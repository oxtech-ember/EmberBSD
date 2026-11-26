# SPDX-License-Identifier: BSD-2-Clause
# The evbarm kernel_noload payload starts 64 bytes after its uImage header.
# Its entry point must have the same low 20 address bits as 0x80000040.
load ${devtype} ${devnum}:${distro_bootpart} ${fdt_addr_r} ${prefix}rk3506b-luckfox-lyra-zero-w.dtb || exit 1
load ${devtype} ${devnum}:${distro_bootpart} 0x02000000 ${prefix}netbsd-GENERIC.ub || exit 1
setenv bootargs root=NAME=netbsd-root
bootm 0x02000000 - ${fdt_addr_r}
