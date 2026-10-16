/* SPDX-License-Identifier: BSD-2-Clause */
/* Host PCI/info model. Extracted lookup retains the NetBSD BSD notice. */
@LICENSE@
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
@OPTION@
@ENUMS@
#define BIT(x) (1u << (x))
struct intel_device_info { unsigned gen, require_force_probe, ppgtt_size, engine_mask; enum intel_platform platform; };
#define GEN11_FEATURES .gen = 11
#define PLATFORM(x) .platform = (x)
@INFO@
struct pci_attach_args { unsigned pa_id, pa_class, pa_function; };
struct pci_device_id { unsigned vendor, device; uintptr_t driver_data; };
static struct intel_device_info info;
static struct pci_device_id entries[1];
static const struct pci_device_id *const i915_device_ids = entries;
static const size_t i915_n_device_ids = 1;
#define PCI_VENDOR(x) ((x) & 0xffff)
#define PCI_PRODUCT(x) ((x) >> 16)
#define PCI_CLASS(x) ((x) >> 24)
#define PCI_VENDOR_INTEL 0x8086
#define PCI_CLASS_DISPLAY 3
static unsigned rejected;
#define printf(...) rejected++
@LOOKUP@
int main(void)
{
 unsigned count = 0;
 assert(jsl_info.platform == INTEL_JASPERLAKE && jsl_info.gen == 11);
 assert(jsl_info.require_force_probe == 1 && jsl_info.ppgtt_size == 36 && jsl_info.engine_mask == 0x47);
 const unsigned vendors[] = {0x8086, 0x1002};
 const unsigned products[] = {0x4e55, 0x4e51, 0xffff};
 const enum intel_platform platforms[] = {INTEL_JASPERLAKE, INTEL_ELKHARTLAKE, INTEL_ICELAKE};
 for (unsigned v = 0; v < 2; v++) for (unsigned p = 0; p < 3; p++)
 for (unsigned d = 0; d < 2; d++) for (unsigned f = 0; f < 2; f++)
 for (unsigned plat = 0; plat < 3; plat++) for (unsigned gen = 10; gen <= 11; gen++)
 for (unsigned force = 0; force < 2; force++) for (unsigned table_vendor = 0; table_vendor < 2; table_vendor++)
 for (unsigned width = 0; width < 2; width++) for (unsigned mask = 0; mask < 2; mask++) {
  info = jsl_info;
  info.platform = platforms[plat]; info.gen = gen; info.require_force_probe = force;
  info.ppgtt_size = width ? 48 : 36; info.engine_mask = mask ? 0x4f : 0x47;
  entries[0] = (struct pci_device_id){ vendors[table_vendor], p == 2 ? 0x4e55 : products[p], (uintptr_t)&info };
  struct pci_attach_args pa = { (products[p] << 16) | vendors[v], (d ? 2u : 3u) << 24, f };
  bool match = v == 0 && p != 2 && d == 0 && f == 0;
  bool trial = EXPECT_TRIAL && p == 0 && plat == 0 && gen == 11 && table_vendor == 0 && width == 0 && mask == 0;
  bool expected = match && (!force || trial);
  const struct pci_device_id *found = i915drmkms_pci_lookup(&pa);
  assert((found != NULL) == expected);
  if (found) assert(found == &entries[0]);
  count++;
 }
 assert(count == 2304);
 puts("2304 exact-identity/option/require-flag matrix cases PASS");
 return 0;
}
