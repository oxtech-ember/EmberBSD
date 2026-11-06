#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Extract unchanged FreeBSD rtw88 source from commit
# a143197801c680b958ab662c88cf086668a1958b; see donor license below.
# Usage: python3 rtw8821c-table-trace-ref.py DONOR_DIR > reference.c
# Compile reference.c with the SDK-selected native compiler and execute it.
import hashlib
import pathlib
import re
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: rtw8821c-table-trace-ref.py DONOR_DIR')
root = pathlib.Path(sys.argv[1])
checksums = {
    'phy.c': '669d76c8acfc9f30418dbd119e777eae4404f6e3bb1198292aab536a3aa9e4ec',
    'main.h': 'bab159780b164959b5bd981d24544507558ed816334d49fd1a31d4d17a69285e',
    'rtw8821c_table.c': 'ebc2547ddd66b7df434e0ef91319eddf5565c452863c4a0fd64660839d3e9a0c',
}
for name, expected in checksums.items():
    if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
        raise SystemExit('unexpected donor SHA-256: ' + name)
phy = (root / 'phy.c').read_text()
header = (root / 'main.h').read_text()
tables = (root / 'rtw8821c_table.c').read_text()


def extract_function(name):
    pattern = r'^(?:static )?(?:void|bool)\s+' + name + r'\([^\n]*(?:\n[^\{]*)?\n\{.*?^\}'
    match = re.search(pattern, phy, re.M | re.S)
    if not match:
        raise SystemExit('missing donor function: ' + name)
    return match.group(0)


print('''// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 *
 * Donor: FreeBSD rtw88, commit a143197801c680b958ab662c88cf086668a1958b.
 * Functions and tables below are extracted unchanged. The surrounding
 * model records writes and waits; it does not emulate hardware.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define __LITTLE_ENDIAN 1
#elif __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error Unsupported host byte order
#endif
#define __packed __attribute__((packed))
#define BIT(n) (1U << (n))
#define BUILD_BUG_ON(x) _Static_assert(!(x), "size")
#define EXPORT_SYMBOL(x)
#define rtw_dbg(...) ((void)0)
typedef uint32_t u32;
typedef uint8_t u8;
''')
print(header[header.index('struct rtw_phy_cond {'):header.index('struct rtw_fifo_conf {')])
print('''enum {
    RTW_CHIP_TYPE_8812A = 1, RTW_CHIP_TYPE_8821A, RTW_CHIP_TYPE_8821C,
    RTW_HCI_TYPE_PCIE = 0, RTW_HCI_TYPE_USB, RTW_HCI_TYPE_SDIO
};
struct rtw_hal {
    unsigned cut_version;
    struct rtw_phy_cond phy_cond;
    struct rtw_phy_cond2 phy_cond2;
};
struct rtw_efuse {
    u8 rfe_option, ext_lna_2g, ext_pa_2g, ext_lna_5g, ext_pa_5g;
    u8 btcoex, alna_type, glna_type, apa_type, gpa_type;
};
struct chip { int id; };
struct rtw_dev { struct rtw_hal hal; struct rtw_efuse efuse; struct chip *chip; };
#define rtw_hci_type(d) RTW_HCI_TYPE_PCIE
struct rtw_table {
    const void *data;
    unsigned size;
    void (*do_cfg)(struct rtw_dev *, const struct rtw_table *, u32, u32);
};
''')
print(phy[phy.index('struct phy_cfg_pair {'):phy.index('static const u32 db_invert_table')])
for name in ('rtw_phy_setup_phy_cond', 'check_positive', 'rtw_parse_tbl_phy_cond'):
    print(extract_function(name))
for name in ('mac', 'bb', 'agc', 'agc_btg_type2', 'rf_a'):
    match = re.search(r'static const u32 rtw8821c_' + name + r'\[\]\s*=\s*\{.*?\};', tables, re.S)
    if not match:
        raise SystemExit('missing donor table: ' + name)
    print(match.group(0))
print('''
static uint64_t hash;
static unsigned count, kind;

static void
event(uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t values[] = { a, b, c };
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < 8; j++) {
            hash ^= (values[i] >> (j * 8)) & 255;
            hash *= UINT64_C(1099511628211);
        }
    }
    count++;
}

/* Recording model of the native writer's register and delay operations. */
static void
cfg(struct rtw_dev *dev, const struct rtw_table *table, u32 addr, u32 data)
{
    static const unsigned delays[] = { 1, 5, 50, 1000, 5000, 50000 };
    (void)dev;
    (void)table;
    if (kind == 1) {
        event(1, addr, data & 255);
    } else if (kind == 2 && addr >= 0xf9 && addr <= 0xfe) {
        event(5, delays[addr - 0xf9], 0);
    } else if (kind == 4) {
        if (addr == 0xffe || addr == 0xfe) {
            event(5, addr == 0xffe ? 50000 : 100, 0);
        } else {
            event(4, 0xc90, ((addr & 255) << 20) | (data & 0xfffff));
            event(5, 13, 0);
            event(5, 1, 0);
        }
    } else {
        event(4, addr, data);
    }
}

int
main(void)
{
    static const unsigned profiles[][3] = {
        { 0, 0, 0 }, { 2, 0, 0 }, { 4, 1, 1 },
        { 6, 2, 0 }, { 0, 4, 0 }, { 0, 4, 1 }
    };
    const u32 *arrays[] = { rtw8821c_mac, rtw8821c_bb, rtw8821c_agc,
        rtw8821c_agc_btg_type2, rtw8821c_rf_a };
    const unsigned sizes[] = { sizeof(rtw8821c_mac), sizeof(rtw8821c_bb),
        sizeof(rtw8821c_agc), sizeof(rtw8821c_agc_btg_type2),
        sizeof(rtw8821c_rf_a) };
    struct chip chip = { RTW_CHIP_TYPE_8821C };
    struct rtw_dev dev = { .chip = &chip };

    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        unsigned rfe = profiles[i][0], cut = profiles[i][1], pkg = profiles[i][2];
        hash = UINT64_C(14695981039346656037);
        count = 0;
        dev.hal.cut_version = cut;
        dev.efuse.rfe_option = rfe;
        rtw_phy_setup_phy_cond(&dev, pkg);
        for (unsigned n = 0; n < 5; n++) {
            struct rtw_table table = { arrays[n], sizes[n] / sizeof(u32), cfg };
            if (n == 3 && rfe != 2 && rfe != 4)
                continue;
            kind = n == 0 ? 1 : n == 1 ? 2 : n == 4 ? 4 : 3;
            rtw_parse_tbl_phy_cond(&dev, &table);
        }
        printf("%u %u %u %u 0x%016llx\\n", rfe, cut, pkg, count,
            (unsigned long long)hash);
    }
    return 0;
}''')
