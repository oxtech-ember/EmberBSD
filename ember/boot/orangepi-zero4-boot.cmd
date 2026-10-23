# U-Boot script for the Orange Pi Zero 4 and Zero 3W (Allwinner A733).
#
# The board vendor's U-Boot looks for boot.scr on the first partition of
# the card. An image builder wraps this file into that script and puts the
# native kernel and both device trees beside it. The tree gets spare room
# before booti, because the boot loader adds its own properties to it.
#
# The same card starts both boards, and the boot loader does not name the
# one it runs on. The Zero 3W has no Ethernet PHY, so the pins of the MAC
# (PH0 to PH14) float there: read as inputs they follow the pull resistors
# both up and down. On the Zero 4 the PHY and its straps hold them. Only a
# board that follows both pulls gets the Zero 3W tree. The pins are put
# back the way they were found.
# Each loader entry selects its mode before it sources this shared script.
# An unset mode preserves normal boot. Diagnostic mode is specific to Zero 3W.
if test -z "${ember_boot_mode}"; then setenv ember_boot_mode normal; fi
if test "${ember_boot_mode}" != normal -a "${ember_boot_mode}" != diagnostic; then
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
fi
# Never enter the kernel with stale memory after a failed load or FDT command.
load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} ${prefix}dtb/allwinner/${fdtname} || exit 1
fdt addr ${fdt_addr_r} || exit 1
fdt resize 8192 || exit 1
load ${devtype} ${devnum}:${distro_bootpart} 0x44000000 ${prefix}netbsd.img || exit 1
booti 0x44000000 - ${fdt_addr_r}
