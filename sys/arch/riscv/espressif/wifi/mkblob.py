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
espwifi_ext_set_igtk espwifi_ext_sta_pmf_configured'''.split()


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


def absolute(value):
    p = Path(value)
    if not p.is_absolute():
        raise argparse.ArgumentTypeError('path must be absolute')
    return p


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
    for opt in ('idf-source', 'wpa-source', 'sysroot', 'host-prefix', 'target-binutils-prefix', 'output'):
        p.add_argument('--'+opt, type=absolute, required=True)
    a = p.parse_args()
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
             '-isystem', str(a.sysroot/'usr/include'), '-isystem', str(resource/'include')]
    for pattern in INCLUDES:
        for d in sorted(glob.glob(str(idf/pattern))):
            if Path(d).is_dir():
                flags += ['-idirafter', d]
    units = [(HERE/'glue.c', []), (HERE/'wpa_log.c', []),
             (HERE/'fullmac_wpa2.c', ['-I'+str(idf/'wpa_supplicant/esp_supplicant/src'),
                '-I'+str(a.wpa_source/'src'),
                '-I'+str(a.wpa_source/'src/rsn_supp')])]
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
        args += ['-o', output] + objects + ['--start-group'] + libs + ['--end-group', '-T', HERE/'espwifi.lds']
        for f in ROM:
            args += ['-T', idf/'esp_rom/esp32s31/ld'/f]
        args += ['-T', idf/'soc/esp32s31/ld/esp32s31.peripherals.ld']
        run(args, env)
        check_vendor_elf(output.read_bytes())
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
