/* Origin: EmberBSD; AI-assisted native DRM metadata schema. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DRM_NATIVE_IDENTITY_H_
#define _DRM_NATIVE_IDENTITY_H_

/* Native-endian uint32_t words; no pointers, padding, strings or dev_t. */
#define DRM_NATIVE_PCI_VERSION 1
#define DRM_NATIVE_BUS_PCI 1
#define DRM_NATIVE_PRIMARY 1
#define DRM_NATIVE_RENDER 2
#define DRM_NATIVE_REVISION 4

struct drm_native_pci_record {
	uint32_t version, length, flags, bus_type;
	uint32_t domain, bus, device, function;
	uint32_t vendor, product, subvendor, subproduct, revision;
	uint32_t primary_major, primary_minor, render_major, render_minor;
};

/* Separate leaf/schema: the PCI version-1 ABI remains unchanged. */
#define DRM_NATIVE_PLATFORM_VERSION 1
#define DRM_NATIVE_BUS_PLATFORM 2
#define DRM_NATIVE_PLATFORM_NAME_LEN 64
struct drm_native_platform_record {
	uint32_t version, length, flags, bus_type;
	uint32_t primary_major, primary_minor, render_major, render_minor;
	char fullname[DRM_NATIVE_PLATFORM_NAME_LEN];
	char compatible[DRM_NATIVE_PLATFORM_NAME_LEN];
};

#endif
