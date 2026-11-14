#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""
select618.py LINUX_TREE OUTDIR

Select the files of Linux 6.18 that make up the DRM core import for EmberBSD
(sys/external/bsd/drm2/dist618) and classify the licence of each.

Sources: drivers/gpu/drm/*.c, drivers/gpu/drm/display, drivers/gpu/drm/clients,
drivers/gpu/buddy.c, and the headers include/drm/*.h (+ display, clients),
include/uapi/drm, include/linux/gpu_buddy.h.  The set of object files is
computed from the Linux Makefiles for the configuration in CONFIG below
(an x86 PCI machine without device tree, bridges, panels, MIPI, kunit).

Writes into OUTDIR:
  select.tsv    path  kind(c|h|uapi)  class  spdx  action  reason
  objs.txt      the .c files of the selected configuration, one per line
  summary.txt   counts per class and action

class:   MIT        MIT or BSD/ISC text or SPDX
         DUAL       SPDX with "OR MIT"/"OR BSD"
         UAPI-NOTE  GPL-2.0 WITH Linux-syscall-note (userland interface header)
         GPL        GPL-2.0 only (SPDX or text)
         NONE       no licence text (kernel default: GPL-2.0)
action:  import     goes to dist618 (MIT, DUAL)
         ref        not imported; kept in the reference tree for the compile
                    map only (GPL, UAPI-NOTE, NONE): needs a BSD rewrite or a
                    decision of the owner
         skip       not part of the configuration (kept out of both)
"""
import os, re, sys, json, collections

root, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
DRM = 'drivers/gpu/drm'

# x86 PCI configuration, no OF/DT, no bridge/panel/MIPI; TTM and the scheduler
# are included (their Makefiles are not read: the lists below are fixed,
# without kunit tests and without ttm_agp_backend.c, CONFIG_AGP is off).
CONFIG = {
 'DRM': 'y', 'PCI': 'y', 'COMPAT': 'y', 'DRM_KMS_HELPER': 'y',
 'DRM_CLIENT': 'y', 'DRM_CLIENT_LIB': 'y', 'DRM_FBDEV_EMULATION': 'y',
 'DRM_PANEL': 'y', 'DRM_PRIVACY_SCREEN': 'y',
 'DRM_PANEL_ORIENTATION_QUIRKS': 'y', 'DRM_PANEL_BACKLIGHT_QUIRKS': 'y',
 'DRM_EXEC': 'y', 'DRM_GPUVM': 'y', 'DRM_BUDDY': 'y',
 'DRM_GEM_DMA_HELPER': 'y', 'DRM_GEM_SHMEM_HELPER': 'y',
 'DRM_SUBALLOC_HELPER': 'y',
 'DRM_TTM': 'y', 'DRM_TTM_HELPER': 'y', 'DRM_SCHED': 'y',
 'DRM_DISPLAY_HELPER': 'y', 'DRM_DISPLAY_DP_HELPER': 'y',
 'DRM_DISPLAY_DSC_HELPER': 'y', 'DRM_DISPLAY_HDCP_HELPER': 'y',
 'DRM_DISPLAY_HDMI_HELPER': 'y', 'DRM_DISPLAY_HDMI_STATE_HELPER': 'y',
 'DRM_DISPLAY_HDMI_AUDIO_HELPER': 'y',
}
# Left out on purpose (value: reason).
OFF = {
 'OF': 'no device tree on x86', 'DEBUG_FS': 'no debugfs in the layer',
 'DRM_ACCEL': 'compute accelerators', 'DRM_PANIC': 'needs font/zlib/logo',
 'DRM_DRAW': 'only for DRM_PANIC', 'DRM_PANIC_SCREEN_QR_CODE': 'Rust',
 'DRM_LOAD_EDID_FIRMWARE': 'GPL-only helper, firmware EDID not needed',
 'DRM_VRAM_HELPER': 'VRAM helper for simple PCI devices, GPL-only',
 'DRM_MIPI_DBI': 'SPI panels', 'DRM_MIPI_DSI': 'DSI panels',
 'DRM_GPUSVM': 'SVM, nouveau staging only', 'ZONE_DEVICE': 'no',
 'DRM_PANEL_BRIDGE': 'bridges', 'DRM_BRIDGE_CONNECTOR': 'bridges',
 'DRM_CLIENT_LOG': 'drm_log needs font/panic',
 'DRM_DISPLAY_DP_AUX_BUS': 'device tree', 'DRM_DISPLAY_DP_TUNNEL': 'Thunderbolt',
 'DRM_DISPLAY_DP_AUX_CHARDEV': 'userspace AUX', 'DRM_DISPLAY_DP_AUX_CEC': 'CEC',
 'DRM_DISPLAY_HDMI_CEC_HELPER': 'CEC', 'DRM_DISPLAY_HDMI_CEC_NOTIFIER_HELPER': 'CEC',
 'DRM_LEGACY': 'removed upstream',
}

def read(p):
    return open(os.path.join(root, p), errors='replace').read()

def logical_lines(text):
    cur = ''
    for ln in text.split('\n'):
        ln = re.sub(r'(^|\s)#.*$', '', ln).rstrip()
        if ln.endswith('\\'):
            cur += ln[:-1] + ' '
            continue
        cur += ln
        if cur.strip():
            yield cur
        cur = ''

def expand(s):
    # $(CONFIG_X) -> value ('' when off)
    return re.sub(r'\$\(CONFIG_([A-Za-z0-9_]+)\)', lambda m: CONFIG.get(m.group(1), ''), s)

def parse_makefile(path):
    var = collections.defaultdict(list)
    for ln in logical_lines(read(path)):
        m = re.match(r'^\s*([A-Za-z0-9_\-\$\(\)]+)\s*(:=|\+=|=)\s*(.*)$', ln)
        if not m:
            continue
        name, op, val = m.groups()
        if 'call' in val or 'shell' in val or name.startswith(('CFLAGS', 'subdir', 'hdrtest', 'always', 'quiet', 'cmd')):
            continue
        name = expand(name)
        # a name that still has a $(...) or ends with '-' (config off) is not enabled
        if '$(' in name or name.endswith('-'):
            continue
        toks = [t for t in expand(val).split() if t]
        if op == ':=':
            var[name] = toks
        else:
            var[name] += toks
    return var

def objects(makefile, topvars):
    """Resolve obj-y / obj-m style targets and composite objects into .c names (relative to the Makefile dir)."""
    var = parse_makefile(makefile)
    res, seen = [], set()
    def add_obj(o):
        if o in seen:
            return
        seen.add(o)
        if o.endswith('/'):
            return
        base = o[:-2] if o.endswith('.o') else o
        comp = var.get(base + '-y', []) + var.get(base + '-objs', []) + var.get(base + '-m', [])
        if comp:
            for c in comp:
                add_obj(c)
        else:
            res.append(base + '.c')
    for k in ('obj-y', 'obj-m'):
        for o in var.get(k, []):
            add_obj(o)
    for k in list(var):
        pass
    return res

P4_TTM = ['ttm_backup.c', 'ttm_bo.c', 'ttm_bo_util.c', 'ttm_bo_vm.c', 'ttm_device.c',
          'ttm_execbuf_util.c', 'ttm_module.c', 'ttm_pool.c', 'ttm_range_manager.c',
          'ttm_resource.c', 'ttm_sys_manager.c', 'ttm_tt.c']
P4_SCHED = ['sched_entity.c', 'sched_fence.c', 'sched_main.c']

def build_set():
    items = []   # (path, reason-if-skip or None)
    # root: composite 'drm' (drm-y) and 'drm_kms_helper' etc come through obj-y
    mk = os.path.join(DRM, 'Makefile')
    var = parse_makefile(mk)
    # obj-$(CONFIG_DRM)+=drm.o -> composite drm-y ; make sure CONFIG_DRM resolves
    for o in objects(mk, var):
        if '/' in o:
            continue
        items.append(os.path.join(DRM, o))
    for sub in ('display', 'clients'):
        smk = os.path.join(DRM, sub, 'Makefile')
        for o in objects(smk, None):
            if '/' in o:
                continue
            items.append(os.path.join(DRM, sub, o))
    items.append('drivers/gpu/buddy.c')
    # TTM, the GPU scheduler, the TTM GEM helper.
    for f in P4_TTM:
        items.append(DRM + '/ttm/' + f)
    for f in P4_SCHED:
        items.append(DRM + '/scheduler/' + f)
    items.append(DRM + '/drm_gem_ttm_helper.c')
    return sorted(set(items))

spdx = re.compile(r'SPDX-License-Identifier:\s*(.*?)\s*(?:\*/)?\s*$', re.M)

def classify(text):
    h = text[:3500]
    m = spdx.search(h)
    if m:
        s = m.group(1).strip()
        su = s.upper()
        if 'WITH LINUX-SYSCALL-NOTE' in su and 'MIT' not in su and 'BSD' not in su:
            return 'UAPI-NOTE', s
        if re.search(r'\bOR\s+(MIT|BSD)', su) or su in ('MIT', 'ISC') or su.startswith(('BSD', 'MIT')):
            return ('DUAL' if 'GPL' in su else 'MIT'), s
        if 'GPL' in su:
            return 'GPL', s
        return 'MIT', s
    hl = h.lower()
    if 'permission is hereby granted, free of charge' in hl or 'redistribution and use in source and binary forms' in hl or 'permission to use, copy, modify' in hl:
        return 'MIT', 'text'
    if 'gnu general public license' in hl or 'gpl' in hl:
        return 'GPL', 'text'
    return 'NONE', '-'

def action_of(cls):
    return 'import' if cls in ('MIT', 'DUAL') else 'ref'

rows = []
srcs = build_set()
objs = []
for p in srcs:
    if not os.path.exists(os.path.join(root, p)):
        rows.append((p, 'c', 'MISSING', '-', 'skip', 'listed by the Makefile, not present'))
        continue
    cls, sp = classify(read(p))
    rows.append((p, 'c', cls, sp, action_of(cls), 'objs'))
    objs.append(p)

def headers(rel, kind, recurse=False):
    base = os.path.join(root, rel)
    for d, ds, fs in os.walk(base):
        if not recurse:
            ds[:] = []
        for f in sorted(fs):
            if f.endswith('.h'):
                p = os.path.relpath(os.path.join(d, f), root)
                yield p, kind

hdrs = []
hdrs += list(headers(DRM, 'h'))                       # root-local headers
hdrs += list(headers(DRM + '/display', 'h'))
hdrs += list(headers(DRM + '/clients', 'h'))
hdrs += list(headers('include/drm', 'h'))
hdrs += list(headers('include/drm/display', 'h'))
hdrs += list(headers('include/drm/clients', 'h'))
hdrs += list(headers('include/drm/ttm', 'h'))
hdrs += list(headers(DRM + '/ttm', 'h'))
hdrs += list(headers(DRM + '/scheduler', 'h'))
hdrs += list(headers('include/uapi/drm', 'uapi'))
if os.path.exists(os.path.join(root, 'include/linux/gpu_buddy.h')):
    hdrs.append(('include/linux/gpu_buddy.h', 'h'))
# headers excluded on purpose: other-vendor subtrees are not scanned (no recursion)
for p, kind in sorted(set(hdrs)):
    cls, sp = classify(read(p))
    rows.append((p, kind, cls, sp, action_of(cls), '-'))

with open(os.path.join(out, 'select.tsv'), 'w') as f:
    f.write('path\tkind\tclass\tspdx\taction\treason\n')
    for r in rows:
        f.write('\t'.join(r) + '\n')
with open(os.path.join(out, 'objs.txt'), 'w') as f:
    f.write('\n'.join(objs) + '\n')
cnt = collections.Counter((r[1], r[2], r[4]) for r in rows)
with open(os.path.join(out, 'summary.txt'), 'w') as f:
    for k in sorted(cnt):
        f.write('%s\t%s\t%s\t%d\n' % (k[0], k[1], k[2], cnt[k]))
    f.write('config on: %s\n' % ' '.join(sorted(CONFIG)))
    f.write('config off: %s\n' % ' '.join('%s(%s)' % (k, v) for k, v in sorted(OFF.items())))
print(open(os.path.join(out, 'summary.txt')).read())
