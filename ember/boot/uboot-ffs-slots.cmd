# Install as boot.scr.uimg on a provisioned two-slot image. The existing
# boot.scr remains the loader's recovery entry. Marker files contain one byte.
# The vendor FAT writer needs a leaf name for files in the partition root.
setenv ember_slot
for record in osgood.dat osback.dat; do
    if test -z "${ember_slot}" && fatload ${devtype} ${devnum}:${distro_bootpart} 0x43300000 ${record} 2; then
        if test "${filesize}" = 1 && setexpr.b value *0x43300000; then
            if test "${value}" = 41; then setenv ember_slot A; fi
            if test "${value}" = 42; then setenv ember_slot B; fi
        fi
    fi
done
if test -z "${ember_slot}"; then exit 1; fi
setenv ember_trial no
setenv candidate
if fatload ${devtype} ${devnum}:${distro_bootpart} 0x43300000 osnext.dat 2; then
    if test "${filesize}" = 1 && setexpr.b value *0x43300000; then
        if test "${value}" = 41; then setenv candidate A; fi
        if test "${value}" = 42; then setenv candidate B; fi
    fi
fi
if test -n "${candidate}" -a "${candidate}" != "${ember_slot}"; then
    if mw.b 0x43300000 2d 1 && fatwrite ${devtype} ${devnum}:${distro_bootpart} 0x43300000 osnext.dat 1; then
        if mw.b 0x43300000 0 1 && fatload ${devtype} ${devnum}:${distro_bootpart} 0x43300000 osnext.dat 2; then
            if test "${filesize}" = 1; then
                if setexpr.b value *0x43300000 && test "${value}" = 2d; then
                    setenv ember_trial yes
                    setenv ember_slot ${candidate}
                fi
            fi
        fi
    fi
fi
if load ${devtype} ${devnum}:${distro_bootpart} 0x43200000 ${prefix}slotboot.scr; then
    setenv ember_saved_prefix "${prefix}"
    source 0x43200000
    setenv prefix "${ember_saved_prefix}"
    setenv ember_saved_prefix
fi
if test "${ember_trial}" = yes; then reset; fi
setenv ember_slot
setenv ember_trial
