/* SPDX-License-Identifier: BSD-2-Clause */
/* Host output/state model. Extracted code retains Intel's MIT notices. */
@LICENSES@
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
@ENUMS@
typedef uint8_t u8;
#define __packed
#define __stringify_1(x) #x
#define __stringify(x) __stringify_1(x)
#define ARRAY_SIZE(x) ((int)(sizeof(x) / sizeof((x)[0])))
#define IS_ENABLED(x) 1
static unsigned errors;
#define pr_err(...) errors++
@DEFS@
struct intel_uc_fw { enum intel_uc_fw_type type; const char *path; u8 major_ver_wanted, minor_ver_wanted; };
static struct { int enable_guc; } i915_modparams = {@DEFAULT@};
@SELECTOR@
static void check(enum intel_platform p, u8 rev, enum intel_uc_fw_type type, const char *path, u8 major, u8 minor) {
 struct intel_uc_fw fw = { .type = type };
 errors = 0; __uc_fw_auto_select(&fw, p, rev);
 assert(errors == 0);
 assert(fw.path != NULL && strcmp(fw.path, path) == 0);
 assert(fw.major_ver_wanted == major && fw.minor_ver_wanted == minor);
}
int main(void) {
 assert(i915_modparams.enable_guc == 0);
 for (unsigned r = 0; r <= 255; r++) {
  check(INTEL_JASPERLAKE, r, INTEL_UC_FW_TYPE_GUC, "i915/ehl_guc_33.0.4.bin", 33, 0);
  check(INTEL_JASPERLAKE, r, INTEL_UC_FW_TYPE_HUC, "i915/ehl_huc_9.0.0.bin", 9, 0);
  check(INTEL_ELKHARTLAKE, r, INTEL_UC_FW_TYPE_GUC, "i915/ehl_guc_33.0.4.bin", 33, 0);
  check(INTEL_ELKHARTLAKE, r, INTEL_UC_FW_TYPE_HUC, "i915/ehl_huc_9.0.0.bin", 9, 0);
 }
 check(INTEL_ICELAKE, 0, INTEL_UC_FW_TYPE_GUC, "i915/icl_guc_33.0.0.bin", 33, 0);
 check(INTEL_TIGERLAKE, 0, INTEL_UC_FW_TYPE_GUC, "i915/tgl_guc_35.2.0.bin", 35, 2);
 check(INTEL_SKYLAKE, 0, INTEL_UC_FW_TYPE_HUC, "i915/skl_huc_2.0.0.bin", 2, 0);
 check(INTEL_COFFEELAKE, 4, INTEL_UC_FW_TYPE_GUC, "i915/kbl_guc_33.0.0.bin", 33, 0);
 check(INTEL_COFFEELAKE, 5, INTEL_UC_FW_TYPE_GUC, "i915/cml_guc_33.0.0.bin", 33, 0);
 struct intel_uc_fw missing = { .type = INTEL_UC_FW_TYPE_GUC };
 __uc_fw_auto_select(&missing, INTEL_I830, 0); assert(missing.path == NULL);
 puts("JSL firmware metadata, table ordering and unchanged disabled GuC default: PASS"); return 0;
}
