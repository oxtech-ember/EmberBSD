/* RISC-V floating-point register transfers, using 64-bit ABI slots. */
#ifndef _RISCV_FPU_REGOPS_H_
#define _RISCV_FPU_REGOPS_H_

#define RISCV_FPU32_STORE \
	".option push\n\t.option arch,+f\n\t" \
	"fsw\tf0, (0*%1)(%0)\n\t" \
	"fsw\tf1, (1*%1)(%0)\n\t" \
	"fsw\tf2, (2*%1)(%0)\n\t" \
	"fsw\tf3, (3*%1)(%0)\n\t" \
	"fsw\tf4, (4*%1)(%0)\n\t" \
	"fsw\tf5, (5*%1)(%0)\n\t" \
	"fsw\tf6, (6*%1)(%0)\n\t" \
	"fsw\tf7, (7*%1)(%0)\n\t" \
	"fsw\tf8, (8*%1)(%0)\n\t" \
	"fsw\tf9, (9*%1)(%0)\n\t" \
	"fsw\tf10, (10*%1)(%0)\n\t" \
	"fsw\tf11, (11*%1)(%0)\n\t" \
	"fsw\tf12, (12*%1)(%0)\n\t" \
	"fsw\tf13, (13*%1)(%0)\n\t" \
	"fsw\tf14, (14*%1)(%0)\n\t" \
	"fsw\tf15, (15*%1)(%0)\n\t" \
	"fsw\tf16, (16*%1)(%0)\n\t" \
	"fsw\tf17, (17*%1)(%0)\n\t" \
	"fsw\tf18, (18*%1)(%0)\n\t" \
	"fsw\tf19, (19*%1)(%0)\n\t" \
	"fsw\tf20, (20*%1)(%0)\n\t" \
	"fsw\tf21, (21*%1)(%0)\n\t" \
	"fsw\tf22, (22*%1)(%0)\n\t" \
	"fsw\tf23, (23*%1)(%0)\n\t" \
	"fsw\tf24, (24*%1)(%0)\n\t" \
	"fsw\tf25, (25*%1)(%0)\n\t" \
	"fsw\tf26, (26*%1)(%0)\n\t" \
	"fsw\tf27, (27*%1)(%0)\n\t" \
	"fsw\tf28, (28*%1)(%0)\n\t" \
	"fsw\tf29, (29*%1)(%0)\n\t" \
	"fsw\tf30, (30*%1)(%0)\n\t" \
	"fsw\tf31, (31*%1)(%0)\n\t" \
	".option pop\n\t"

#define RISCV_FPU32_LOAD \
	".option push\n\t.option arch,+f\n\t" \
	"flw\tf0, (0*%1)(%0)\n\t" \
	"flw\tf1, (1*%1)(%0)\n\t" \
	"flw\tf2, (2*%1)(%0)\n\t" \
	"flw\tf3, (3*%1)(%0)\n\t" \
	"flw\tf4, (4*%1)(%0)\n\t" \
	"flw\tf5, (5*%1)(%0)\n\t" \
	"flw\tf6, (6*%1)(%0)\n\t" \
	"flw\tf7, (7*%1)(%0)\n\t" \
	"flw\tf8, (8*%1)(%0)\n\t" \
	"flw\tf9, (9*%1)(%0)\n\t" \
	"flw\tf10, (10*%1)(%0)\n\t" \
	"flw\tf11, (11*%1)(%0)\n\t" \
	"flw\tf12, (12*%1)(%0)\n\t" \
	"flw\tf13, (13*%1)(%0)\n\t" \
	"flw\tf14, (14*%1)(%0)\n\t" \
	"flw\tf15, (15*%1)(%0)\n\t" \
	"flw\tf16, (16*%1)(%0)\n\t" \
	"flw\tf17, (17*%1)(%0)\n\t" \
	"flw\tf18, (18*%1)(%0)\n\t" \
	"flw\tf19, (19*%1)(%0)\n\t" \
	"flw\tf20, (20*%1)(%0)\n\t" \
	"flw\tf21, (21*%1)(%0)\n\t" \
	"flw\tf22, (22*%1)(%0)\n\t" \
	"flw\tf23, (23*%1)(%0)\n\t" \
	"flw\tf24, (24*%1)(%0)\n\t" \
	"flw\tf25, (25*%1)(%0)\n\t" \
	"flw\tf26, (26*%1)(%0)\n\t" \
	"flw\tf27, (27*%1)(%0)\n\t" \
	"flw\tf28, (28*%1)(%0)\n\t" \
	"flw\tf29, (29*%1)(%0)\n\t" \
	"flw\tf30, (30*%1)(%0)\n\t" \
	"flw\tf31, (31*%1)(%0)\n\t" \
	".option pop\n\t"

#define RISCV_FPU64_STORE \
	".option push\n\t.option arch,+d\n\t" \
	"fsd\tf0, (0*%1)(%0)\n\t" \
	"fsd\tf1, (1*%1)(%0)\n\t" \
	"fsd\tf2, (2*%1)(%0)\n\t" \
	"fsd\tf3, (3*%1)(%0)\n\t" \
	"fsd\tf4, (4*%1)(%0)\n\t" \
	"fsd\tf5, (5*%1)(%0)\n\t" \
	"fsd\tf6, (6*%1)(%0)\n\t" \
	"fsd\tf7, (7*%1)(%0)\n\t" \
	"fsd\tf8, (8*%1)(%0)\n\t" \
	"fsd\tf9, (9*%1)(%0)\n\t" \
	"fsd\tf10, (10*%1)(%0)\n\t" \
	"fsd\tf11, (11*%1)(%0)\n\t" \
	"fsd\tf12, (12*%1)(%0)\n\t" \
	"fsd\tf13, (13*%1)(%0)\n\t" \
	"fsd\tf14, (14*%1)(%0)\n\t" \
	"fsd\tf15, (15*%1)(%0)\n\t" \
	"fsd\tf16, (16*%1)(%0)\n\t" \
	"fsd\tf17, (17*%1)(%0)\n\t" \
	"fsd\tf18, (18*%1)(%0)\n\t" \
	"fsd\tf19, (19*%1)(%0)\n\t" \
	"fsd\tf20, (20*%1)(%0)\n\t" \
	"fsd\tf21, (21*%1)(%0)\n\t" \
	"fsd\tf22, (22*%1)(%0)\n\t" \
	"fsd\tf23, (23*%1)(%0)\n\t" \
	"fsd\tf24, (24*%1)(%0)\n\t" \
	"fsd\tf25, (25*%1)(%0)\n\t" \
	"fsd\tf26, (26*%1)(%0)\n\t" \
	"fsd\tf27, (27*%1)(%0)\n\t" \
	"fsd\tf28, (28*%1)(%0)\n\t" \
	"fsd\tf29, (29*%1)(%0)\n\t" \
	"fsd\tf30, (30*%1)(%0)\n\t" \
	"fsd\tf31, (31*%1)(%0)\n\t" \
	".option pop\n\t"

#define RISCV_FPU64_LOAD \
	".option push\n\t.option arch,+d\n\t" \
	"fld\tf0, (0*%1)(%0)\n\t" \
	"fld\tf1, (1*%1)(%0)\n\t" \
	"fld\tf2, (2*%1)(%0)\n\t" \
	"fld\tf3, (3*%1)(%0)\n\t" \
	"fld\tf4, (4*%1)(%0)\n\t" \
	"fld\tf5, (5*%1)(%0)\n\t" \
	"fld\tf6, (6*%1)(%0)\n\t" \
	"fld\tf7, (7*%1)(%0)\n\t" \
	"fld\tf8, (8*%1)(%0)\n\t" \
	"fld\tf9, (9*%1)(%0)\n\t" \
	"fld\tf10, (10*%1)(%0)\n\t" \
	"fld\tf11, (11*%1)(%0)\n\t" \
	"fld\tf12, (12*%1)(%0)\n\t" \
	"fld\tf13, (13*%1)(%0)\n\t" \
	"fld\tf14, (14*%1)(%0)\n\t" \
	"fld\tf15, (15*%1)(%0)\n\t" \
	"fld\tf16, (16*%1)(%0)\n\t" \
	"fld\tf17, (17*%1)(%0)\n\t" \
	"fld\tf18, (18*%1)(%0)\n\t" \
	"fld\tf19, (19*%1)(%0)\n\t" \
	"fld\tf20, (20*%1)(%0)\n\t" \
	"fld\tf21, (21*%1)(%0)\n\t" \
	"fld\tf22, (22*%1)(%0)\n\t" \
	"fld\tf23, (23*%1)(%0)\n\t" \
	"fld\tf24, (24*%1)(%0)\n\t" \
	"fld\tf25, (25*%1)(%0)\n\t" \
	"fld\tf26, (26*%1)(%0)\n\t" \
	"fld\tf27, (27*%1)(%0)\n\t" \
	"fld\tf28, (28*%1)(%0)\n\t" \
	"fld\tf29, (29*%1)(%0)\n\t" \
	"fld\tf30, (30*%1)(%0)\n\t" \
	"fld\tf31, (31*%1)(%0)\n\t" \
	".option pop\n\t"

#endif
