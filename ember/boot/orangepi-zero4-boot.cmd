# U-Boot script for the Orange Pi Zero 4 and Zero 3W (Allwinner A733).
#
# The board vendor's U-Boot looks for boot.scr on the first partition of
# the card. An image builder wraps this file into that script and puts the
# native kernel and device trees beside it. The tree gets spare room
# before booti, because the boot loader adds its own properties to it.
#
# The same card starts both boards, and the boot loader does not name the
# one it runs on. The Zero 3W has no Ethernet PHY, so the pins of the MAC
# (PH0 to PH14) float there: read as inputs they follow the pull resistors
# both up and down. On the Zero 4 the PHY and its straps hold them. Only a
# board that follows both pulls gets the Zero 3W tree. The pins are put
# back the way they were found.
# Each loader entry selects its mode before it sources this shared script.
# An unset mode preserves normal boot.
# Diagnostic selects Zero 3W. Graphics and npu-observe select Zero 4.
if test -z "${ember_boot_mode}"; then setenv ember_boot_mode normal; fi
if test "${ember_boot_mode}" != normal -a "${ember_boot_mode}" != diagnostic -a "${ember_boot_mode}" != graphics -a "${ember_boot_mode}" != npu-observe; then
    echo "Invalid EmberBSD boot mode"
    exit 1
fi
setenv fdt_addr_r 0x4fa00000
setenv bootargs "root=NAME=netbsd-root"
setenv fdtname sun60i-a733-orangepi-zero4.dtb
mw.l 0x02000400 0
mw.l 0x02000404 0
mw.l 0x02000430 0x55555555
sleep 0.1
setexpr.l pins_up *0x02000410 \& 0x7fff
mw.l 0x02000430 0xaaaaaaaa
sleep 0.1
setexpr.l pins_down *0x02000410 \& 0x7fff
mw.l 0x02000430 0
mw.l 0x02000400 0xffffffff
mw.l 0x02000404 0xffffffff
if test "${pins_up}" = "7fff" -a "${pins_down}" = "0"; then setenv fdtname sun60i-a733-orangepi-zero3w.dtb; fi
if test "${ember_boot_mode}" = diagnostic; then
    if test "${fdtname}" != sun60i-a733-orangepi-zero3w.dtb; then
        echo "Diagnostic DTB requires Zero 3W"
        exit 1
    fi
    setenv fdtname sun60i-a733-orangepi-zero3w-diagnostic.dtb
elif test "${ember_boot_mode}" = graphics; then
    if test "${fdtname}" != sun60i-a733-orangepi-zero4.dtb; then
        echo "Graphics DTB requires Zero 4"
        exit 1
    fi
    setenv fdtname sun60i-a733-orangepi-zero4-graphics.dtb
elif test "${ember_boot_mode}" = npu-observe; then
    if test "${fdtname}" != sun60i-a733-orangepi-zero4.dtb; then
        echo "NPU observe DTB requires Zero 4"
        exit 1
    fi
    setenv fdtname sun60i-a733-orangepi-zero4-npu-observe.dtb
fi
# The two-slot loader sets ember_slot only on provisioned normal images.
if test -n "${ember_slot}"; then
    if test "${ember_boot_mode}" != normal; then exit 1; fi
    if test "${fdtname}" != sun60i-a733-orangepi-zero3w.dtb; then exit 1; fi
    if test "${ember_slot}" = B; then
        setenv bootargs "root=NAME=ember-root-b"
        setenv prefix "${prefix}os-b/"
    elif test "${ember_slot}" != A; then
        exit 1
    fi
fi
if test "${ember_trial}" = yes; then
    if test -z "${ember_slot}"; then exit 1; fi
    # Configure the A733 system-reset watchdog for a 16-second handoff.
    mw.l 0x02050010 0x16aa0001
    mw.l 0x02050014 0x16aa00b1
    mw.l 0x0205000c 0x14af
    setexpr.l mode *0x02050014 \& 1
    setexpr.l config *0x02050010 \& 3
    if test "${mode}" != 1 -o "${config}" != 1; then exit 1; fi
fi
# Never enter the kernel with stale memory after a failed load or FDT command.
load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} ${prefix}dtb/allwinner/${fdtname} || exit 1
fdt addr ${fdt_addr_r} || exit 1
fdt resize 8192 || exit 1
load ${devtype} ${devnum}:${distro_bootpart} 0x44000000 ${prefix}netbsd.img || exit 1
if test "${ember_trial}" = yes; then
    if test -z "${ember_slot}"; then exit 1; fi
    fdt set /chosen emberbsd,trial-watchdog || exit 1
    mw.l 0x0205000c 0x14af
fi
booti 0x44000000 - ${fdt_addr_r}
