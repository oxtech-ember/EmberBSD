/* SPDX-License-Identifier: BSD-2-Clause */
/* Production code is inserted by i915-jsl-platform.rb with its licenses. */
@LICENSES@
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef uint8_t u8;
#define BIT(x) (1U << (x))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BITS_PER_TYPE(t) (8 * sizeof(t))
#define BUILD_BUG_ON(x) _Static_assert(!(x), #x)
#define INTEL_SUBPLATFORM_BITS 3
static unsigned int warnings;
static bool
fixture_warn(bool condition)
{
	if (condition)
		warnings++;
	return condition;
}
#define WARN_ON(x) fixture_warn(x)
#define WARN_ON_ONCE(x) WARN_ON(x)
#define drm_dbg_kms(...) ((void)0)
@ENUMS@
struct pci_device_id {
	u32 vendor, device, subvendor, subdevice, class, class_mask;
	unsigned long driver_data;
};
struct intel_device_info {
	enum intel_platform platform;
	unsigned int gen, require_force_probe, engine_mask, ppgtt_size;
};
struct sseu_dev_info {
	unsigned int max_slices, max_subslices, max_eus_per_subslice;
	unsigned int has_slice_pg, has_subslice_pg, has_eu_pg;
};
struct intel_runtime_info {
	u32 platform_mask[2];
	struct sseu_dev_info sseu;
};
struct drm_i915_private {
	const struct intel_device_info *info;
	struct intel_runtime_info runtime;
};
#define INTEL_INFO(d) ((d)->info)
#define RUNTIME_INFO(d) (&(d)->runtime)
@MASK@
@PREDICATES@
/* Unchanged generation/subplatform branches outside the JSL scenarios. */
#define IS_GEN(d, n) (INTEL_INFO(d)->gen == (n))
#define IS_HSW_ULT(d) false
#define IS_BDW_ULT(d) false
@IDS@
#define PLATFORM(x) .platform = (x)
/* Inherited Gen11 fields are checked by the full target TU compilation. */
#define GEN11_FEATURES .gen = 11
@INFOS@
@STOLEN@
@NAMES@
static void
intel_sseu_set_info(struct sseu_dev_info *s, unsigned int a,
    unsigned int b, unsigned int c)
{
	s->max_slices = a;
	s->max_subslices = b;
	s->max_eus_per_subslice = c;
}
#define GEN11_GT_SLICE_ENABLE 1
#define GEN11_GT_SUBSLICE_DISABLE 2
#define GEN11_EU_DISABLE 3
#define GEN11_GT_S_ENA_MASK 1
#define GEN11_EU_DIS_MASK 255
static u32
fixture_read(unsigned int reg)
{
	return reg == GEN11_GT_SLICE_ENABLE ? 1U : 0U;
}
#define I915_READ(r) fixture_read(r)
static void
gen11_compute_sseu_info(struct sseu_dev_info *s, u8 a, u32 b, u8 c)
{
	(void)s;
	assert(a == 1 && b == UINT32_MAX && c == 255);
}
@SSEU@
@PCH@
static const struct pci_device_id *const i915_device_ids = pciidlist;
static const size_t i915_n_device_ids = ARRAY_SIZE(pciidlist);
struct pci_attach_args { unsigned int pa_function, pa_id, pa_class; };
#define PCI_VENDOR(v) ((v) & 65535)
#define PCI_PRODUCT(v) ((v) >> 16)
#define PCI_CLASS(v) ((v) >> 24)
#define PCI_VENDOR_INTEL 0x8086
#define PCI_CLASS_DISPLAY 3
@LOOKUP@
static const struct pci_device_id *
find_id(const struct pci_device_id *ids, size_t count, unsigned int device)
{
	for (size_t i = 0; i < count; i++)
		if (ids[i].vendor == 0x8086 && ids[i].device == device)
			return &ids[i];
	return NULL;
}
static struct drm_i915_private
device(const struct intel_device_info *info)
{
	struct drm_i915_private d = { .info = info };
	unsigned int p = info->platform;
	d.runtime.platform_mask[p / 29] = BIT(p % 29 + 3);
	return d;
}
int
main(void)
{
	const struct pci_device_id *ent = find_id(pciidlist, ARRAY_SIZE(pciidlist), 0x4e55);
	if (!ent) {
		fputs("FAIL: 8086:4e55 has no dedicated JSL identity\n", stderr);
		return 1;
	}
	const struct intel_device_info *jsl = (const void *)ent->driver_data;
	assert(!strcmp(intel_platform_name(jsl->platform), "JASPERLAKE"));
	assert(jsl != &ehl_info && jsl->platform != ehl_info.platform);
	assert(jsl->gen == 11 && jsl->require_force_probe == 1);
	assert(jsl->engine_mask == 0x47 && jsl->ppgtt_size == 36);
	assert(!find_id(pciidlist, ARRAY_SIZE(pciidlist), 0x4e57));
	struct drm_i915_private d = device(jsl);
	gen11_sseu_info_init(&d);
	assert(d.runtime.sseu.max_slices == 1 && d.runtime.sseu.max_subslices == 4);
	assert(d.runtime.sseu.max_eus_per_subslice == 8 && d.runtime.sseu.has_eu_pg == 1);
	assert(intel_pch_type(&d, 0x4d80) == PCH_JSP && warnings == 0);
	assert(intel_pch_type(&d, 0x3880) == PCH_JSP && warnings == 0);
	assert(intel_pch_type(&d, 0x4b00) == PCH_MCC && warnings == 0);
	assert(intel_virt_detect_pch(&d) == 0x4b00);
	assert(intel_pch_type(&d, 0x3480) == PCH_ICP && warnings == 1);
	warnings = 0;
	d = device(&ehl_info);
	gen11_sseu_info_init(&d);
	assert(d.runtime.sseu.max_subslices == 4);
	assert(intel_pch_type(&d, 0x4b00) == PCH_MCC && warnings == 0);
	assert(intel_virt_detect_pch(&d) == 0x4b00);
	d = device(&icl_info);
	gen11_sseu_info_init(&d);
	assert(d.runtime.sseu.max_subslices == 8);
	assert(intel_pch_type(&d, 0x3480) == PCH_ICP && warnings == 0);
	assert(intel_virt_detect_pch(&d) == 0x3480);
	ent = find_id(intel_stolen_ids, ARRAY_SIZE(intel_stolen_ids), 0x4e55);
	assert(ent && (const void *)ent->driver_data == &gen11_stolen_ops);
	assert(find_id(pciidlist, ARRAY_SIZE(pciidlist), 0x4e51)->driver_data == (unsigned long)&ehl_info);
	/* The actual native lookup must still reject preliminary JSL and EHL. */
	struct pci_attach_args pa = { .pa_id = 0x4e558086, .pa_class = 0x03000000 };
	assert(i915drmkms_pci_lookup(&pa) == NULL);
	pa.pa_id = 0x45008086;
	assert(i915drmkms_pci_lookup(&pa) == NULL);
	pa.pa_id = 0x8a508086;
	assert(i915drmkms_pci_lookup(&pa) != NULL);
	pa.pa_function = 1;
	assert(i915drmkms_pci_lookup(&pa) == NULL);
	pa.pa_function = 0;
	pa.pa_id = 0x4e551234;
	assert(i915drmkms_pci_lookup(&pa) == NULL);
	puts("PASS: production JSL identity, SSEU/PCH/stolen consumers; native attachment remains closed");
	return 0;
}
