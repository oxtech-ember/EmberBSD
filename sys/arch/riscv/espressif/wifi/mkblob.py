#!/usr/bin/env python3
"""Build an ESP32-S31 radio relocatable object from selected shared inputs."""
import argparse
import glob
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
CORE = '''esp_hw_support/modem/modem_clock.c
esp_hw_support/modem/port/esp32s31/modem_clock_impl.c
hal/esp32s31/modem_clock_hal.c
esp_hw_support/periph_ctrl.c
esp_hal_regi2c/esp32s31/regi2c_impl.c
esp_wifi/src/ftm_load_calibration.c
esp_hw_support/regi2c_ctrl.c
esp_phy/src/phy_common.c
esp_phy/esp32s31/phy_init_data.c
esp_wifi/regulatory/esp_wifi_regulatory.c'''.split()
CRYPTO = '''aes-cbc.c aes-ccm.c aes-gcm.c aes-omac1.c aes-unwrap.c aes-wrap.c
sha1.c sha1-internal.c sha1-pbkdf2.c sha1-prf.c sha256.c sha256-internal.c
sha256-prf.c aes-internal.c aes-internal-dec.c aes-internal-enc.c'''.split()
INCLUDES = '''*/include */esp32s31/include */esp32s31
soc/esp32s31/register hal/platform_port/include esp_hw_support/modem/include
esp_hw_support/include/soc esp_hw_support/port/esp32s31/include
esp_hw_support/port/esp32s31/private_include esp_hw_support/port/include
esp_wifi/include/local esp_phy/esp32s31/include esp_rom/esp32s31/include/esp32s31
log/include heap/include riscv/include'''.split()
ROM = '''esp32s31.rom.ld esp32s31.rom.api.ld esp32s31.rom.libgcc.ld
esp32s31.rom.rvfp.ld esp32s31.rom.version.ld esp32s31.rom.pp.ld
esp32s31.rom.net80211.ld esp32s31.rom.phy.ld esp32s31.rom.coexist.ld
esp32s31.rom.libc.ld'''.split()
ENTRIES = '''espwifi_start_wpa2 espwifi_scan espwifi_seen espwifi_linked
espwifi_connect espwifi_tx espwifi_get_mac espwifi_scan_capture
espwifi_disconnect espwifi_ext_connect espwifi_ext_set_key
espwifi_ext_clear_assoc_appies espwifi_ext_set_igtk
espwifi_ext_sta_pmf_configured'''.split()
NATIVE_WPA_SOURCES = ('wpa_supplicant/src/rsn_supp/wpa.c',
                      'wpa_supplicant/esp_supplicant/src/esp_wpa_main.c')
NATIVE_WPA_SYMBOLS = frozenset(('wpa_set_profile', 'wpa_set_bss',
                                'wpa_set_passphrase', 'esp_supplicant_init'))


def check_consumer_boundary(entries, header, consumers):
    """Ensure every radio call made by kernel sources is exported by blob."""
    text = Path(header).read_text()
    boundary = text.split('/* Entry points of the radio code;', 1)[-1]
    declared = set(re.findall(
        r'\b(?:int|void)\s+(espwifi_[a-z0-9_]+)\s*\(', boundary))
    called = set()
    for source in consumers:
        called.update(re.findall(
            r'\b(espwifi_[a-z0-9_]+)\s*\(', Path(source).read_text()))
    # espwifi_start is only the legacy branch; the experimental producer
    # selects espwifi_start_wpa2 in espwifi_main (the kernel's #ifdef).
    required = (called & declared) - {'espwifi_start'}
    missing = required - set(entries)
    if missing:
        raise ValueError('radio producer omits kernel consumer entries: ' +
                         ', '.join(sorted(missing)))
    unknown = set(entries) - declared
    if unknown:
        raise ValueError('radio producer exports undeclared entries: ' +
                         ', '.join(sorted(unknown)))


def check_external_auth_boundary(units, idf, defined=()):
    """Keep the external-auth object free of IDF's native WPA state machine."""
    forbidden = {(idf / source).resolve() for source in NATIVE_WPA_SOURCES}
    included = sorted(str(Path(source).resolve()) for source, _ in units
                      if Path(source).resolve() in forbidden)
    if included:
        raise ValueError('native IDF WPA source in external-auth radio: ' +
                         ', '.join(included))
    linked = NATIVE_WPA_SYMBOLS.intersection(defined)
    if linked:
        raise ValueError('native IDF WPA symbols in external-auth radio: ' +
                         ', '.join(sorted(linked)))


def defined_symbols_for(path, nm, env):
    """Return global and local defined symbols for the external-auth guard."""
    result = subprocess.check_output([str(nm), '--defined-only',
        '--format=posix', str(path)], env=env, text=True)
    return {line.split()[0] for line in result.splitlines() if line.split()}


def absolute(value):
    p = Path(value)
    if not p.is_absolute():
        raise argparse.ArgumentTypeError('path must be absolute')
    return p


def radio_diagnostic_flags(args):
    """Return opt-in radio diagnostics after validating their dependencies."""
    if args.vendor_rx_observe_once and not args.vendor_debug_once:
        raise ValueError('--vendor-rx-observe-once requires --vendor-debug-once')
    if args.vendor_rx_chain_once and not args.vendor_debug_once:
        raise ValueError('--vendor-rx-chain-once requires --vendor-debug-once')
    if args.vendor_legacy_phy and not args.vendor_rx_chain_once:
        raise ValueError('--vendor-legacy-phy requires --vendor-rx-chain-once')
    flags = []
    if args.vendor_debug_once:
        flags.append('-DESPWIFI_VENDOR_DEBUG_ONCE')
    if args.vendor_rx_observe_once:
        flags.append('-DESPWIFI_RX_OBSERVE_ONCE')
    if args.vendor_rx_chain_once:
        flags.append('-DESPWIFI_RX_CHAIN_ONCE')
    if args.vendor_legacy_phy:
        flags.append('-DESPWIFI_VENDOR_LEGACY_PHY')
    return flags


def run(cmd, env):
    subprocess.run([str(x) for x in cmd], env=env, check=True)


def check_mac_leaf(disassembly):
    # This entry is used during attach before an FPU-owning task exists.
    # Fail closed if a compiler makes it call another function or use FP.
    instructions = []
    for line in disassembly.splitlines():
        if not re.match(r'^\s*[0-9a-f]+:', line):
            continue
        m = re.match(r'^\s*[0-9a-f]+:\s+[0-9a-f]{4,8}\s+([\w.]+)', line)
        if not m:
            raise ValueError('unrecognized instruction in espwifi_get_mac')
        instructions.append(m.group(1))
    if not instructions or instructions[-1] != 'ret' or set(instructions) - {
            'lui', 'mv', 'lw', 'srli', 'sb', 'ret'}:
        raise ValueError('espwifi_get_mac is no longer the verified integer-only leaf')


def check_vendor_elf(data):
    if (len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01' or
            struct.unpack_from('<H', data, 16)[0] != 1 or
            struct.unpack_from('<H', data, 18)[0] != 243 or
            struct.unpack_from('<I', data, 36)[0] & 6 != 2 or
            struct.unpack_from('<I', data, 36)[0] & 8):
        raise ValueError('expected an ELF32 RISC-V single-float relocatable object')


def check_f_only(disassembly):
    for line in disassembly.splitlines():
        match = re.match(r'^\s*[0-9a-fA-F]+:\s+(\S+)', line)
        if match:
            op = match.group(1)
            if op in ('fld', 'fsd', 'c.fld', 'c.fsd', 'c.fldsp', 'c.fsdsp') or (op.startswith('f') and
                    'd' in op.split('.')[1:]):
                raise ValueError('double-precision instruction in F-only radio: ' + op)


RX_CHAIN_WRAPS = {
    'sta_input': ('net80211_funcs_init', 88, 0x2f800cfc),
    'ieee80211_crypto_decap': ('net80211_funcs_init', 96, 0x2f800cb0),
    'ieee80211_decap': ('net80211_funcs_init', 100, 0x2f800cb4),
    'sta_rx_eapol': ('net80211_funcs_init', 104, 0x2f800d00),
    'ppRxProtoProc': ('wdev_funcs_init', 0x198, 0x2f800fac),
}
RX_CHAIN_WRAP_SYMBOLS = tuple(RX_CHAIN_WRAPS) + (
    'sta_rx_cb', 'get_sublen_offset', 'wdev_funcs_init')


def check_rx_chain_wrappers(path, objdump, nm, env):
    """Verify net80211 wrappers and the normal STA receive callback chain."""
    symbols = subprocess.check_output([str(nm), '--defined-only', '--print-size',
        '--format=posix', str(path)], env=env, text=True)
    addresses = {}
    sizes = {}
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) >= 3:
            try:
                addresses[fields[0]] = int(fields[2], 16)
                sizes[fields[0]] = int(fields[3], 16) if len(fields) >= 4 else 0
            except ValueError:
                continue

    def disassemble(symbol):
        start = addresses.get(symbol)
        size = sizes.get(symbol)
        if start is None or not size:
            raise ValueError(f'missing sized symbol {symbol} in linked radio object')
        return subprocess.check_output([str(objdump), '-dr',
            f'--start-address=0x{start:x}',
            f'--stop-address=0x{start + size:x}', str(path)],
            env=env, text=True)

    for symbol, (init_symbol, slot, rom_address) in RX_CHAIN_WRAPS.items():
        init = disassemble(init_symbol)
        reloc = re.compile(rf'R_RISCV_HI20\s+__wrap_{re.escape(symbol)}\b')
        matches = list(reloc.finditer(init))
        if not matches:
            all_relocs = subprocess.check_output([str(objdump), '-r',
                str(path)], env=env, text=True)
            relevant = [line for line in all_relocs.splitlines()
                if re.search(rf'\b(?:__wrap_|__real_)?{re.escape(symbol)}\b', line)]
            excerpt = '\n'.join(relevant[:8])
            raise ValueError(init_symbol + ' has no relocation through ' +
                f'__wrap_{symbol} for table slot +{slot}' +
                f'\nrelated relocations:\n{excerpt}')
        if symbol == 'ppRxProtoProc':
            # wdev_funcs_init uses a subtable base at a0 + 0x180, so the
            # function is stored at local offset 0x18 (absolute slot +0x198).
            store = r'\b(?:c\.)?sw\s+\w+,\s*(?:24|0x18)\(a4\)'
            base = r'\baddi\s+a4,\s*a0,\s*(?:384|0x180)'
        else:
            slot_offset = rf'(?:{slot}|0x{slot:x})'
            store = rf'\b(?:c\.)?sw\s+\w+,\s*{slot_offset}\(a0\)'
            base = None
        if not any(re.search(store, init[m.end():m.end() + 320])
                   for m in matches):
            raise ValueError(f'__wrap_{symbol} relocation is not followed by ' +
                             f'the table slot +{slot} store')
        if base is not None and not re.search(base, init):
            raise ValueError('wdev_funcs_init does not establish the pinned ' +
                             f'subtable base for +{slot:#x}')

        wrapper = disassemble('__wrap_' + symbol)
        real_reloc = re.compile(rf'R_RISCV_[A-Z0-9_]+\s+{re.escape(symbol)}\b')
        if not real_reloc.search(wrapper):
            raise ValueError(f'__wrap_{symbol} does not chain to ROM ' +
                             f'address 0x{rom_address:08x}')
        if addresses.get(symbol) != rom_address:
            raise ValueError(f'{symbol} resolves to {addresses.get(symbol)!r}, ' +
                             f'expected ROM address 0x{rom_address:08x}')

    proto_wrapper = disassemble('__wrap_ppRxProtoProc')
    if not re.search(r'R_RISCV_[A-Z0-9_]+\s+ppRxProtoProc\b',
                     proto_wrapper):
        raise ValueError('__wrap_ppRxProtoProc does not tail-chain to ROM')
    for pattern, label in (
            (r'\bbeqz\s+a0\b', 'ESF'),
            (r'\bbeqz\s+a1\b', 'RX-control'),
            (r'\blw\s+t0,\s*(?:4|0x4)\(a0\)', 'ESF buffer'),
            (r'\blw\s+t0,\s*(?:4|0x4)\(t0\)', 'packet buffer'),
            (r'\blhu\s+t1,\s*(?:36|0x24)\(a0\)', 'ESF flags'),
            (r'\blbu\s+t1,\s*(?:3|0x3)\(a1\)', 'rxmatch0'),
            (r'\blbu\s+t2,\s*(?:0|0x0)\(t0\)', 'FC byte'),
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+'
             r'espwifi_rx_chain_pp_proto_fc_match_count\b', 'FC matrix')):
        if not re.search(pattern, proto_wrapper):
            raise ValueError('__wrap_ppRxProtoProc lacks its pinned ' + label +
                             ' read')
    if sizes.get('espwifi_rx_chain_pp_proto_fc_match_count') != 32:
        raise ValueError('PP FC/match counters are not an eight-element '
                         'uint32_t array')
    if not re.search(r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+'
                     r'espwifi_rx_chain_pp_proto_invalid_count\b',
                     proto_wrapper):
        raise ValueError('__wrap_ppRxProtoProc lacks the invalid-path counter')

    wdev_init_wrapper = disassemble('__wrap_wdev_funcs_init')
    if not re.search(r'R_RISCV_CALL(?:_PLT)?\s+wdev_funcs_init\b',
                     wdev_init_wrapper):
        raise ValueError('__wrap_wdev_funcs_init does not chain to ROM/vendor init')
    for pattern, label in (
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+pp_wdev_funcs\b',
             'callback table pointer'),
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+__wrap_get_sublen_offset\b',
             'helper wrapper'),
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+get_sublen_offset\b',
             'original helper address')):
        if not re.search(pattern, wdev_init_wrapper):
            raise ValueError('__wrap_wdev_funcs_init lacks its ' + label)
    if not re.search(r'\blw\s+t2,\s*(?:1044|0x414)\(t0\)',
                     wdev_init_wrapper) or not re.search(
            r'\bbne\s+t2,\s*t1\b', wdev_init_wrapper):
        raise ValueError('__wrap_wdev_funcs_init does not verify the original '
                         'helper slot before replacement')
    if not re.search(r'\b(?:sw|c\.sw)\s+\w+,\s*(?:1044|0x414)\(t0\)',
                     wdev_init_wrapper):
        raise ValueError('__wrap_wdev_funcs_init does not install the helper '
                         'in pp_wdev_funcs + 0x414')
    if not re.search(r'R_RISCV_CALL(?:_PLT)?\s+__wrap_wdev_funcs_init\b',
                     disassemble('esp_wifi_init_internal')):
        raise ValueError('esp_wifi_init_internal does not invoke the wrapped '
                         'wdev table initializer')

    sublen_wrapper = disassemble('__wrap_get_sublen_offset')
    if len(re.findall(r'R_RISCV_CALL(?:_PLT)?\s+get_sublen_offset\b',
                      sublen_wrapper)) != 2:
        raise ValueError('__wrap_get_sublen_offset must have one selected '
                         'call and one bypass tail-chain relocation')
    for pattern, label in (
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+wDevCtrl_ptr\b',
             'control pointer'),
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+'
             r'espwifi_rx_chain_sublen_fc_match_count\b', 'FC/match array'),
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+'
             r'espwifi_rx_chain_sublen_data_state_count\b', 'DATA-state array'),
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+'
             r'espwifi_rx_chain_sublen_invalid_count\b', 'invalid counter'),
            (r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+'
             r'espwifi_rx_chain_sublen_short_count\b', 'short counter')):
        if not re.search(pattern, sublen_wrapper):
            raise ValueError('__wrap_get_sublen_offset lacks its ' + label)
    if sizes.get('espwifi_rx_chain_sublen_fc_match_count') != 32 or \
            sizes.get('espwifi_rx_chain_sublen_data_state_count') != 1024:
        raise ValueError('sub-length counters are not 8x/256x uint32 arrays')
    if addresses.get('get_sublen_offset') != 0x2f8011e0 or \
            addresses.get('wDev_ProcessRxSucData') != 0x2f8010f4:
        raise ValueError('pinned sub-length helper/caller ROM alias changed')

    # wifi_create_sta registers the normal callback through ifidx zero;
    # ppRegisterRxCallback stores it in pTxRx + 0x3f8 on this pinned ABI.
    create_sta = disassemble('wifi_create_sta')
    callback_reloc = re.compile(r'R_RISCV_HI20\s+__wrap_sta_rx_cb\b')
    callback_refs = list(callback_reloc.finditer(create_sta))
    if not callback_refs:
        raise ValueError('wifi_create_sta does not register __wrap_sta_rx_cb')
    callback_setup = create_sta[callback_refs[-1].start():]
    if not re.search(r'\bli\s+a1,\s*(?:0|0x0)\b', callback_setup) or not re.search(
            r'R_RISCV_CALL(?:_PLT)?\s+ic_register_rx_cb\b', callback_setup):
        raise ValueError('wifi_create_sta callback registration is not the '
                         'ifidx-0 ic_register_rx_cb path')
    if 'sta_rx_cb' not in addresses:
        raise ValueError('missing real sta_rx_cb symbol for wrapper chain')
    if addresses.get('__wrap_sta_rx_cb') == addresses.get('sta_rx_cb'):
        raise ValueError('__wrap_sta_rx_cb aliases the real sta_rx_cb')
    wrapper = disassemble('__wrap_sta_rx_cb')
    if not re.search(r'R_RISCV_[A-Z0-9_]+\s+sta_rx_cb\b', wrapper):
        raise ValueError('__wrap_sta_rx_cb does not tail-chain to sta_rx_cb')
    if not re.search(r'\bbeqz\s+a0\b', wrapper):
        raise ValueError('__wrap_sta_rx_cb lacks null guards for the packet path')
    if len(re.findall(r'\bbeqz\s+t0\b', wrapper)) < 2:
        raise ValueError('__wrap_sta_rx_cb does not null-check both ESF pointers')
    if not re.search(r'\blw\s+t0,\s*(?:4|0x4)\(a0\)', wrapper) or not re.search(
            r'\blw\s+t0,\s*(?:4|0x4)\(t0\)', wrapper):
        raise ValueError('__wrap_sta_rx_cb does not follow the pinned ESF '
                         'buffer pointer path')
    if not re.search(r'\blhu\s+t1,\s*(?:36|0x24)\(a0\)', wrapper):
        raise ValueError('__wrap_sta_rx_cb does not read pinned ESF flags')
    if not re.search(r'\blui\s+t2,\s*(?:2|0x2)\b', wrapper) or not re.search(
            r'\band\s+t1,\s*t1,\s*t2\b', wrapper) or not re.search(
            r'\baddi\s+t0,\s*t0,\s*(?:8|0x8)\b', wrapper):
        raise ValueError('__wrap_sta_rx_cb does not apply the pinned optional '
                         '8-byte header adjustment')
    if not re.search(r'\blbu\s+t2,\s*(?:0|0x0)\(t0\)', wrapper):
        raise ValueError('__wrap_sta_rx_cb does not read the FC byte from the '
                         'resolved packet header')
    if not re.search(r'\bandi\s+t2,\s*t2,\s*(?:12|0xc)\b', wrapper):
        raise ValueError('__wrap_sta_rx_cb does not classify only FC type bits')
    if not re.search(r'\badd\s+t0,\s*t0,\s*t2\b', wrapper):
        raise ValueError('__wrap_sta_rx_cb does not index the FC type array')
    if not re.search(r'R_RISCV_(?:PCREL_|GOT_)?HI20\s+'
                     r'espwifi_rx_chain_sta_rx_cb_type_count\b',
                     wrapper):
        raise ValueError('__wrap_sta_rx_cb does not index the FC type counter array')
    if sizes.get('espwifi_rx_chain_sta_rx_cb_type_count') != 16:
        raise ValueError('FC type counters are not a four-element uint32_t array')
    # The reader uses only t0-t2 and reads a0 without changing caller arguments.
    for line in wrapper.splitlines():
        match = re.match(r'^\s*[0-9a-f]+:\s+[0-9a-f]+\s+([\w.]+)\s+([^,]+)',
                         line)
        if match and (match.group(2).strip() in {
                'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 'a7', 'a0', 'ra', 'sp'} and
                match.group(1) not in {'beqz', 'bnez'}):
            raise ValueError('__wrap_sta_rx_cb modifies caller-owned register ' +
                             match.group(2).strip())


def common_wpa_flags(common, idf):
    return ['-I'+str(common), '-I'+str(common/'utils'), '-I'+str(common/'crypto'),
        '-D__ets__', '-DESP_PLATFORM', '-DESP_SUPPLICANT', '-DIEEE8021X_EAPOL',
        '-DESPRESSIF_USE', '-DCONFIG_IEEE80211W', '-DCONFIG_SAE',
        '-DCONFIG_SHA256', '-DCONFIG_NO_RADIUS', '-DCONFIG_INTERNAL_SHA256',
        '-DCONFIG_CRYPTO_INTERNAL', '-DCONFIG_GMAC=1',
        '-DWLAN_FC_ISWEP=WLAN_FC_PROTECTED', '-DWLAN_FC_ORDER=WLAN_FC_HTC',
        '-idirafter', str(idf/'wpa_supplicant/port/include'),
        '-idirafter', str(idf/'wpa_supplicant/esp_supplicant/include'),
        '-idirafter', str(idf/'wpa_supplicant/esp_supplicant/src'),
        '-idirafter', str(idf/'wpa_supplicant/esp_supplicant/src/crypto')]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--external-wpa2', action='store_true', required=True,
                   help='build only the experimental BSD-supplicant callback path')
    p.add_argument('--vendor-debug-once', action='store_true',
                   help='enable vendor VERBOSE logging for the first fullmac connect attempt')
    p.add_argument('--vendor-rx-observe-once', action='store_true',
                   help='observe selected-peer RX metadata during the first debug attempt')
    p.add_argument('--vendor-rx-chain-once', action='store_true',
                   help='count net80211 RX entry points during the first debug attempt')
    p.add_argument('--vendor-legacy-phy', action='store_true',
                   help='temporarily restrict STA PHY to 802.11b/g for the RX-chain diagnostic')
    for opt in ('idf-source', 'wpa-source', 'sysroot', 'host-prefix', 'target-binutils-prefix', 'output'):
        p.add_argument('--'+opt, type=absolute, required=True)
    a = p.parse_args()
    try:
        diagnostic_flags = radio_diagnostic_flags(a)
    except ValueError as error:
        p.error(str(error))
    idf = a.idf_source / 'components'
    clang = (a.host_prefix / 'bin/clang').resolve(strict=True)
    if not clang.is_relative_to(a.host_prefix.resolve(strict=True)):
        raise SystemExit('clang resolves outside the selected host prefix')
    linker = Path(str(a.target_binutils_prefix) + 'ld').resolve(strict=True)
    objdump = (a.host_prefix / 'bin/llvm-objdump').resolve(strict=True)
    objcopy = (a.host_prefix / 'bin/llvm-objcopy').resolve(strict=True)
    nm = (a.host_prefix / 'bin/llvm-nm').resolve(strict=True)
    # llvm-readelf may be a multicall symlink; preserve argv[0].
    readelf = a.host_prefix / 'bin/llvm-readelf'
    python = Path(sys.executable).resolve(strict=True)
    if not objdump.is_relative_to(a.host_prefix.resolve(strict=True)):
        raise SystemExit('objdump resolves outside the selected host prefix')
    for tool in (objcopy, nm, readelf, python):
        if not tool.resolve(strict=True).is_relative_to(a.host_prefix.resolve(strict=True)):
            raise SystemExit('LLVM tool resolves outside the selected host prefix')
    if not a.sysroot.is_dir() or not idf.is_dir():
        raise SystemExit('source and sysroot directories must exist')
    check_consumer_boundary(ENTRIES, HERE.parent/'esp_wifi_os.h',
        [HERE.parent/'esp_wifi_if.c', HERE.parent/'esp_wifi_os.c'])
    env = os.environ.copy()
    env['PATH'] = str(a.host_prefix / 'bin') + os.pathsep + str(a.host_prefix / 'gnu/bin')
    resource = Path(subprocess.check_output([str(clang), '-print-resource-dir'], env=env, text=True).strip()).resolve(strict=True)
    if not resource.is_relative_to(a.host_prefix.resolve(strict=True)):
        raise SystemExit('clang resource directory is outside the selected host prefix')
    flags = [str(clang), '--target=riscv32--netbsd', '--sysroot='+str(a.sysroot),
             '-nostdinc', '-ffreestanding', '-Os', '-ffunction-sections',
             '-fdata-sections', '-fno-jump-tables', '-march=rv32imafc_zicsr_zifencei',
             '-mabi=ilp32f', '-D__ESP_IDF_NETBSD__', '-DESPWIFI_COMMON_CRYPTO',
             '-DESPWIFI_EXTERNAL_WPA2',
             '-Werror', '-I'+str(HERE/'shim'),
             '-I'+str(HERE.parents[3]),
             '-isystem', str(a.sysroot/'usr/include'), '-isystem', str(resource/'include')]
    flags += diagnostic_flags
    for pattern in INCLUDES:
        for d in sorted(glob.glob(str(idf/pattern))):
            if Path(d).is_dir():
                flags += ['-idirafter', d]
    units = [(HERE/'glue.c', []), (HERE/'wpa_log.c', []),
             (HERE/'fullmac_wpa2.c', ['-I'+str(idf/'wpa_supplicant/esp_supplicant/src'),
                '-I'+str(a.wpa_source/'src'),
                '-I'+str(a.wpa_source/'src/rsn_supp')])]
    if a.vendor_rx_chain_once:
        units.append((HERE/'rx_chain_diag.S', []))
    units += [(idf/s, ['-DCONFIG_ESP_WIFI_FTM_ENABLE=1'] if s.endswith('ftm_load_calibration.c') else []) for s in CORE]
    common = a.wpa_source / 'src'
    common_flags = common_wpa_flags(common, idf)
    units += [(common/'crypto'/s, common_flags) for s in CRYPTO]
    units += [(common/'rsn_supp/wpa_ie.c', common_flags),
              (common/'common/wpa_common.c', common_flags)]
    units += [(HERE/'wpa_ie_bridge.c', common_flags + ['-I'+str(HERE)])]
    units += [(common/'utils/common.c', common_flags)]
    units += [(idf/'wpa_supplicant/src/crypto/ccmp.c', common_flags)]
    units += [(idf/'wpa_supplicant/src/crypto/crypto_ops.c', common_flags +
        ['-include', str(common/'utils/includes.h')])]
    check_external_auth_boundary(units, idf)
    libs = [idf/'esp_wifi/lib/esp32s31/libnet80211.a', idf/'esp_wifi/lib/esp32s31/libpp.a',
            idf/'esp_phy/lib/esp32s31/libphy.a', idf/'esp_wifi/lib/esp32s31/libcore.a']
    for item in [x[0] for x in units] + libs + [idf/'esp_rom/esp32s31/ld'/x for x in ROM]:
        if not item.is_file():
            raise SystemExit('missing required input: '+str(item))
    if not a.output.parent.is_dir():
        raise SystemExit('output parent must already exist')
    with tempfile.TemporaryDirectory(prefix='.espwifi-', dir=a.output.parent) as td:
        td = Path(td); objects = []
        for i, (src, extra) in enumerate(units):
            obj = td/f'{i:03}.o'
            run(flags + extra + ['-c', str(src), '-o', str(obj)], env)
            objects.append(obj)
        output = td/'espwifi.o'
        args = [linker, '-m', 'elf32lriscv', '-r', '--gc-sections',
                '-e', 'espwifi_start_wpa2']
        for sym in ENTRIES:
            args += ['-u', sym]
        if a.vendor_rx_chain_once:
            args += ['--wrap='+sym for sym in RX_CHAIN_WRAP_SYMBOLS]
        args += ['-o', output] + objects + ['--start-group'] + libs + ['--end-group', '-T', HERE/'espwifi.lds']
        for f in ROM:
            args += ['-T', idf/'esp_rom/esp32s31/ld'/f]
        args += ['-T', idf/'soc/esp32s31/ld/esp32s31.peripherals.ld']
        run(args, env)
        check_vendor_elf(output.read_bytes())
        if a.vendor_rx_chain_once:
            try:
                check_rx_chain_wrappers(output, objdump, nm, env)
            except ValueError as error:
                with tempfile.NamedTemporaryFile(
                        prefix=a.output.name + '.rx-chain-check-failed-',
                        suffix='.o', dir=a.output.parent, delete=False) as saved:
                    saved.write(output.read_bytes())
                    failed_object = Path(saved.name)
                raise ValueError(f'{error}; failed checker input saved at '
                                 f'{failed_object}') from error
        disassembly = subprocess.check_output([str(objdump), '-d',
            '--disassemble-symbols=espwifi_get_mac', str(output)], env=env, text=True)
        if '<espwifi_get_mac>:' not in disassembly:
            raise ValueError('missing espwifi_get_mac in linked radio object')
        check_mac_leaf(disassembly)
        # Decode D as well, even if a bad input omitted it from attributes.
        disassembly = subprocess.check_output([str(objdump), '-d',
            '--mattr=+d', '--no-show-raw-insn', str(output)], env=env, text=True)
        check_f_only(disassembly)
        def globals_for(path):
            result = subprocess.check_output([str(nm), '--defined-only', '--extern-only',
                '--format=posix', str(path)], env=env, text=True)
            return {line.split()[0] for line in result.splitlines() if line.split()}
        all_defined = defined_symbols_for(output, nm, env)
        check_external_auth_boundary(units, idf, all_defined)
        defined = globals_for(output)
        if not set(ENTRIES) <= defined:
            raise ValueError('radio object is missing required entry points')
        if 'wpa_parse_wpa_ie' not in defined:
            raise ValueError('selected common WPA parser was not linked')
        localize = td/'localize.txt'
        localize.write_text(''.join(name+'\n' for name in sorted(defined - set(ENTRIES))))
        localized = td/'localized.o'
        run([objcopy, '--localize-symbols='+str(localize), output, localized], env)
        if globals_for(localized) != set(ENTRIES):
            raise ValueError('unexpected globals remain in radio object')
        # Normalizing ELF metadata is only valid after checking every live
        # crossing symbol and comparing the two actual compiler call ABIs.
        run([str(python), str(HERE/'checkabi.py'), '--clang', str(clang),
             '--sysroot', str(a.sysroot), '--object', str(localized),
             '--readelf', str(readelf), '--nm', str(nm)], env)
        data = bytearray(localized.read_bytes())
        check_vendor_elf(data)
        abi_flags = struct.unpack_from('<I', data, 36)[0]
        struct.pack_into('<I', data, 36, abi_flags & ~6)
        localized.write_bytes(data)
        os.replace(localized, a.output)
    print('Built radio object with verified ILP32 boundary:', a.output)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        print('espwifi producer:', e, file=sys.stderr)
        sys.exit(1)
