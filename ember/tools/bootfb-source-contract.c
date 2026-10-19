/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual source contract, EFI functions and libfdt with controlled services. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <setjmp.h>
#include <dev/fdt/bootfb_source.h>
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "bootfb source CHECK %s:%d: %s\n", __FILE__, __LINE__, #x); \
	abort(); } } while (0)
static unsigned char tree[8192];
static const unsigned char absent_wire[32] = {
	0,0,0,1, 0,0,0,1, 0,0,0,3, 0,0,0,0,
	0x80,0,0,0,0,0,0,14, 0,0,0,0,0,0,0,0
};
static struct bootfb_source_record absent = { 1, 3, UINT64_C(0x800000000000000e), 0 };
static void create_tree(void)
{
	CHECK(fdt_create_empty_tree(tree, sizeof(tree)) == 0);
	CHECK(fdt_setprop_string(tree, 0, "compatible", "netbsd,generic-acpi") == 0);
	CHECK(fdt_add_subnode(tree, 0, "chosen") >= 0);
}
static int chosen(void) { int n = fdt_path_offset(tree, "/chosen"); CHECK(n > 0); return n; }
static void set_record(const void *data, int n) { CHECK(fdt_setprop(tree, chosen(), "ember,bootfb-source-v1", data, n) == 0); }
static void expect_query_error(size_t capacity)
{
	struct bootfb_source_record value;
	unsigned char saved[sizeof(value)];
	memset(&value, 0xa5, sizeof(value)); memcpy(saved, &value, sizeof(saved));
	CHECK(bootfb_source_query(tree, capacity, &value) != 0);
	CHECK(memcmp(saved, &value, sizeof(saved)) == 0);
}
static void wire_tests(void)
{
	unsigned char bytes[48], old[48], malformed[32];
	struct bootfb_source_record value, invalid;
	memset(bytes, 0xa5, sizeof(bytes)); memcpy(old, bytes, sizeof(old));
	CHECK(bootfb_source_encode(&absent, bytes, 31) == EINVAL);
	CHECK(memcmp(bytes, old, sizeof(bytes)) == 0);
	CHECK(bootfb_source_encode(&absent, bytes, sizeof(bytes)) == 0);
	CHECK(memcmp(bytes, absent_wire, 32) == 0 && memcmp(bytes + 32, old + 32, 16) == 0);
	CHECK(bootfb_source_decode(bytes, 32, &value) == 0);
	CHECK(value.producer == 1 && value.disposition == 3 && value.status == absent.status && value.count == 0);
	CHECK(bootfb_source_decode(bytes, 31, &value) == EINVAL && bootfb_source_decode(bytes, 33, &value) == EINVAL);
	CHECK(bootfb_source_encode(NULL, bytes, 48) == EINVAL && bootfb_source_encode(&absent, NULL, 48) == EINVAL);
	CHECK(bootfb_source_decode(NULL, 32, &value) == EINVAL && bootfb_source_decode(bytes, 32, NULL) == EINVAL);
	for (unsigned i = 0; i < 4; i++) {
		memcpy(malformed, absent_wire, 32);
		malformed[(unsigned[]){3,7,11,15}[i]] = (unsigned char)(i == 3 ? 1 : 99);
		unsigned char snapshot[sizeof(value)]; memset(&value, 0xa5, sizeof(value)); memcpy(snapshot, &value, sizeof(snapshot));
		CHECK(bootfb_source_decode(malformed, 32, &value) == EINVAL);
		CHECK(memcmp(snapshot, &value, sizeof(snapshot)) == 0);
	}
	for (unsigned i = 0; i < 3; i++) {
		invalid = absent;
		if (i == 0) invalid.producer = 0;
		if (i == 1) invalid.status = UINT64_C(0x8000000000000002);
		if (i == 2) invalid.count = 1;
		memset(bytes, 0xa5, sizeof(bytes)); memcpy(old, bytes, sizeof(old));
		CHECK(bootfb_source_encode(&invalid, bytes, 48) == EINVAL && memcmp(bytes, old, 48) == 0);
	}
	for (unsigned d = 1; d <= 2; d++) {
		value = (struct bootfb_source_record){1,d,0,1};
		CHECK(bootfb_source_encode(&value, bytes, 32) == 0);
		CHECK(bootfb_source_decode(bytes, 32, &value) == 0 && value.disposition == d && value.count == 1);
	}
	value = (struct bootfb_source_record){0,0,UINT64_MAX,UINT64_MAX};
	CHECK(bootfb_source_encode(&value, bytes, 32) == 0);
	CHECK(bootfb_source_decode(bytes, 32, &value) == 0 && value.status == UINT64_MAX && value.count == UINT64_MAX);
}
static void malformed_tree_tests(void)
{
	for (unsigned kind = 0; kind < 7; kind++) {
		CHECK(fdt_create(tree, sizeof(tree)) == 0);
		CHECK(fdt_finish_reservemap(tree) == 0);
		CHECK(fdt_begin_node(tree, "") == 0);
		CHECK(fdt_property_string(tree, "compatible", "netbsd,generic-acpi") == 0);
		CHECK(fdt_begin_node(tree, kind == 0 ? "chosen@0" : "chosen") == 0);
		CHECK(fdt_property(tree, BOOTFB_SOURCE_PROPERTY, absent_wire, 32) == 0);
		if (kind == 1) CHECK(fdt_property(tree, BOOTFB_SOURCE_PROPERTY, absent_wire, 32) == 0);
		if (kind == 5) CHECK(fdt_property_string(tree, "compatible", "") == 0);
		if (kind == 2) CHECK(fdt_property(tree, "compatible", "bad", 3) == 0);
		if (kind == 3) {
			CHECK(fdt_property_string(tree, "compatible", "test,node") == 0);
			CHECK(fdt_property_string(tree, "compatible", "test,node") == 0);
		}
		CHECK(fdt_end_node(tree) == 0);
		if (kind == 4) { CHECK(fdt_begin_node(tree, "chosen") == 0); CHECK(fdt_end_node(tree) == 0); }
		CHECK(fdt_end_node(tree) == 0);
		if (kind == 6) { CHECK(fdt_begin_node(tree, "") == 0); CHECK(fdt_end_node(tree) == 0); }
		CHECK(fdt_finish(tree) == 0);
		expect_query_error(sizeof(tree));
	}
}
static void mixed_chosen_tests(void)
{
	/* The real libfdt path lookup treats chosen@0 as a /chosen alias. */
	for (unsigned reverse = 0; reverse < 2; reverse++) {
		for (unsigned placement = 0; placement < 3; placement++) {
			CHECK(fdt_create(tree, sizeof(tree)) == 0);
			CHECK(fdt_finish_reservemap(tree) == 0);
			CHECK(fdt_begin_node(tree, "") == 0);
			CHECK(fdt_property_string(tree, "compatible", "netbsd,generic-acpi") == 0);
			for (unsigned sibling = 0; sibling < 2; sibling++) {
				bool alias = (sibling ^ reverse) == 0;
				CHECK(fdt_begin_node(tree, alias ? "chosen@0" : "chosen") == 0);
				if (placement == 2 || (placement == 0) == alias)
					CHECK(fdt_property(tree, BOOTFB_SOURCE_PROPERTY, absent_wire, 32) == 0);
				CHECK(fdt_end_node(tree) == 0);
			}
			CHECK(fdt_end_node(tree) == 0); CHECK(fdt_finish(tree) == 0);
			CHECK(fdt_check_full(tree, sizeof(tree)) == 0);
			expect_query_error(sizeof(tree));
		}
	}
}
static void late_property_test(unsigned compatible)
{
	CHECK(fdt_create(tree, sizeof(tree)) == 0);
	CHECK(fdt_finish_reservemap(tree) == 0);
	CHECK(fdt_begin_node(tree, "") == 0);
	CHECK(fdt_property_string(tree, "compatible", "netbsd,generic-acpi") == 0);
	CHECK(fdt_begin_node(tree, "chosen") == 0);
	CHECK(fdt_property(tree, BOOTFB_SOURCE_PROPERTY, absent_wire, 32) == 0);
	CHECK(fdt_begin_node(tree, "child") == 0); CHECK(fdt_end_node(tree) == 0);
	if (compatible)
		CHECK(fdt_property_string(tree, "compatible", "simple-framebuffer") == 0);
	else
		CHECK(fdt_property(tree, BOOTFB_SOURCE_PROPERTY, absent_wire, 32) == 0);
	CHECK(fdt_end_node(tree) == 0); CHECK(fdt_end_node(tree) == 0);
	CHECK(fdt_finish(tree) == 0);
	/* Actual libfdt full validation accepts this late-property structure. */
	CHECK(fdt_check_full(tree, sizeof(tree)) == 0);
	expect_query_error(sizeof(tree));
}
static void named_root_test(void)
{
	CHECK(fdt_create(tree, sizeof(tree)) == 0);
	CHECK(fdt_finish_reservemap(tree) == 0);
	CHECK(fdt_begin_node(tree, "named-root") == 0);
	CHECK(fdt_property_string(tree, "compatible", "netbsd,generic-acpi") == 0);
	CHECK(fdt_begin_node(tree, "chosen") == 0);
	CHECK(fdt_property(tree, BOOTFB_SOURCE_PROPERTY, absent_wire, 32) == 0);
	CHECK(fdt_end_node(tree) == 0); CHECK(fdt_end_node(tree) == 0);
	CHECK(fdt_finish(tree) == 0);
	CHECK(fdt_check_full(tree, sizeof(tree)) == 0);
	expect_query_error(sizeof(tree));
}
static void tree_tests(void)
{
	struct bootfb_source_record value;
	struct bootfb_source_tree scan;
	create_tree(); CHECK(bootfb_source_scan(tree, sizeof(tree), &scan) == 0 && !scan.record_present && !scan.framebuffer);
	expect_query_error(sizeof(tree)); set_record(absent_wire, 32);
	CHECK(bootfb_source_query(tree, sizeof(tree), &value) == 0 && value.disposition == 3);
	CHECK(fdt_pack(tree) == 0 && bootfb_source_query(tree, fdt_totalsize(tree), &value) == 0);
	expect_query_error(0); expect_query_error(sizeof(struct fdt_header) - 1); expect_query_error(fdt_totalsize(tree) - 1);
	unsigned char oversized[33] = {0}; memcpy(oversized, absent_wire, 32);
	for (int length = 31; length <= 33; length += 2) { create_tree(); set_record(oversized, length); expect_query_error(sizeof(tree)); }
	create_tree(); set_record(absent_wire, 32); CHECK(fdt_setprop_string(tree, 0, "compatible", "unknown,tree") == 0); expect_query_error(sizeof(tree));
	create_tree(); CHECK(fdt_setprop(tree, 0, "ember,bootfb-source-v1", absent_wire, 32) == 0); expect_query_error(sizeof(tree));
	for (int disabled = 0; disabled <= 1; disabled++) {
		create_tree(); set_record(absent_wire, 32);
		int parent = fdt_add_subnode(tree, 0, "outside-chosen"); CHECK(parent > 0);
		int fb = fdt_add_subnode(tree, parent, "framebuffer@0"); CHECK(fb > 0);
		CHECK(fdt_setprop_string(tree, fb, "compatible", "simple-framebuffer") == 0);
		CHECK(fdt_setprop_string(tree, fb, "status", disabled ? "disabled" : "okay") == 0);
		CHECK(fdt_setprop_string(tree, fb, "format", "unknown-format") == 0);
		expect_query_error(sizeof(tree));
	}
	create_tree(); set_record(absent_wire, 32); fdt_set_magic(tree, 0); expect_query_error(sizeof(tree));
	create_tree(); set_record(absent_wire, 32); fdt_set_totalsize(tree, sizeof(tree) + 1); expect_query_error(sizeof(tree));
}
/* EFI/storage services are controlled. The producer functions below are real. */
typedef uint64_t EFI_STATUS;
typedef uint64_t UINTN;
typedef int64_t INTN;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint32_t UINT32;
typedef unsigned long u_long;
typedef void *EFI_HANDLE;
typedef void VOID;
typedef int EFI_LOCATE_SEARCH_TYPE;
typedef struct { unsigned value; } EFI_GUID;
#define IN
#define OUT
#define OPTIONAL
#define EFIERR(x) (UINT64_C(0x8000000000000000) | (x))
#include "efierr.h"
#define ByProtocol 2
#define EFIBOOT_ACPI 1
#define EFIBOOT_RUNTIME_ADDRESS 0
#define WSDISPLAY_BOOTFB_HANDOFF 1
#define ACPI_FDT_SIZE (128 * 1024)
#define FDT_ALIGN (2 * 1024 * 1024)
#define FDT_CHOSEN_NODE_PATH "/chosen"
#define FDT_CHOSEN_NODE_NAME "chosen"
#define ELFDATA2LSB 1
#define MARK_END 0
#define uefi_call_wrapper(fn, n, ...) ((fn)(__VA_ARGS__))
#define EFI_PAGE_SIZE 4096
#define EFI_SIZE_TO_PAGES(n) (((n) + 4095) / 4096)
#define EFI_MEMORY_RUNTIME UINT64_C(0x8000000000000000)
enum { EfiRuntimeServicesCode = 5, EfiRuntimeServicesData, EfiMemoryMappedIO = 11 };
typedef struct {
	uint32_t Type;
	uint64_t PhysicalStart, VirtualStart, NumberOfPages, Attribute;
} EFI_MEMORY_DESCRIPTOR;
#define NextMemoryDescriptor(p, n) ((EFI_MEMORY_DESCRIPTOR *)((char *)(p) + (n)))
enum { PixelRedGreenBlueReserved8BitPerColor, PixelBlueGreenRedReserved8BitPerColor, PixelBitMask, PixelBltOnly };
typedef struct {
	uint32_t HorizontalResolution, VerticalResolution, PixelFormat, PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct {
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
	uint64_t FrameBufferBase, FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct { EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode; } EFI_GRAPHICS_OUTPUT_PROTOCOL;
static EFI_GUID GraphicsOutputProtocol;
static EFI_HANDLE IH;
static EFI_PHYSICAL_ADDRESS efirng_addr;
static u_long efirng_size;
static int netbsd_elf_data = ELFDATA2LSB;
static bool dtoverlay_enabled;
struct efi_cleanup_result;
static int load_file(const char *, u_long, bool, EFI_PHYSICAL_ADDRESS *, u_long *);
static void efi_fdt_set_virtual_address_map_checked(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN, UINTN, UINT32, struct efi_cleanup_result *);
static struct {
	EFI_STATUS locate, protocol, ebs, runtime;
	UINTN count;
	unsigned format;
	uint64_t pa;
	bool null_gop, null_mode, null_info, no_map, no_acpi, no_root;
	bool external, overlay, overlay_load_error, runtime_fb, pack_error;
	int md_error, fail_fdt, fdt_calls, fail_alloc, alloc_calls;
	unsigned locate_calls, protocol_calls, ebs_calls, runtime_calls;
} service;
static void *allocations[256];
static unsigned allocation_count;
static jmp_buf panic_jump;
static bool panic_expected;
static unsigned phase;
static void *fdt_data;
static bool tracking;
static unsigned char final_tree[2 * FDT_ALIGN];
static unsigned char overlay_tree[4096];
static EFI_MEMORY_DESCRIPTOR memory[4];
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
static EFI_GRAPHICS_OUTPUT_PROTOCOL gop;
static EFI_HANDLE handles[64];
static const char *events[128];
static unsigned nevents;
static void event(const char *value) { CHECK(nevents < 128); events[nevents++] = value; }
static void *alloc(size_t size)
{
	service.alloc_calls++;
	if (service.fail_alloc != 0 && service.alloc_calls == service.fail_alloc)
		return NULL;
	CHECK(allocation_count < 256);
	void *p = calloc(1, size); CHECK(p != NULL);
	allocations[allocation_count++] = p; return p;
}
static void dealloc(void *p, size_t size) { (void)p; (void)size; }
static void *AllocatePool(size_t size) { return alloc(size); }
static void panic(const char *format, ...) __attribute__((noreturn));
static void panic(const char *format, ...) { (void)format; if (panic_expected) longjmp(panic_jump, 1); fprintf(stderr, "unexpected producer panic\n"); abort(); }
static bool GrowBuffer(EFI_STATUS *status, void **buffer, UINTN size)
{
	if (*buffer == NULL && (*status == EFI_SUCCESS || *status == EFI_BUFFER_TOO_SMALL)) {
		CHECK(size <= sizeof(handles)); *buffer = handles; return true;
	}
	return false;
}
static EFI_STATUS locate_handles(int type, EFI_GUID *guid, void *key, UINTN *size, EFI_HANDLE *out)
{
	(void)type; (void)guid; (void)key; (void)out;
	service.locate_calls++; CHECK(phase == 0);
	*size = service.count * sizeof(EFI_HANDLE); event("GOP"); return service.locate;
}
static EFI_STATUS handle_protocol(EFI_HANDLE handle, EFI_GUID *guid, void **out)
{
	(void)handle; (void)guid; service.protocol_calls++;
	*out = service.null_gop ? NULL : &gop; return service.protocol;
}
static EFI_STATUS exit_services(EFI_HANDLE image, UINTN key)
{
	(void)image; CHECK(key == 0x1234); CHECK(phase == 0);
	CHECK(fdt_getprop(fdt_data, fdt_path_offset(fdt_data, "/chosen"), BOOTFB_SOURCE_PROPERTY, NULL) == NULL);
	service.ebs_calls++; phase = 1; event("EBS"); return service.ebs;
}
static EFI_STATUS free_pages(EFI_PHYSICAL_ADDRESS pa, UINTN n) { (void)pa; (void)n; return 0; }
static EFI_STATUS set_virtual(UINTN size, UINTN descriptor_size, UINT32 version, void *map);
static struct {
	EFI_STATUS (*LocateHandle)(int, EFI_GUID *, void *, UINTN *, EFI_HANDLE *);
	EFI_STATUS (*HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
	EFI_STATUS (*ExitBootServices)(EFI_HANDLE, UINTN);
	EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
} boot_services = { locate_handles, handle_protocol, exit_services, free_pages }, *BS = &boot_services;
static struct { EFI_STATUS (*SetVirtualAddressMap)(UINTN, UINTN, UINT32, void *); } runtime_services = { set_virtual }, *RT = &runtime_services;
static EFI_MEMORY_DESCRIPTOR *LibMemoryMap(UINTN *n, UINTN *key, UINTN *size, UINT32 *version)
{
	if (service.no_map) return NULL;
	*n = 4; *key = 0x1234; *size = sizeof(memory[0]); *version = 1; return memory;
}
static void efi_cm5_pending(const char *s) { (void)s; }
static void efi_cm5_trace(const char *s) { if (strcmp(s, "[CM5 EFI] cleanup done\r\n") == 0) { phase = 2; event("cleanup"); } }
static bool efi_acpi_available(void) { return !service.no_acpi; }
static void *efi_acpi_root(void) { return service.no_root ? NULL : (void *)(uintptr_t)0x1000; }
static void *efi_acpi_smbios(void) { return NULL; }
static const char *efi_acpi_get_model(void) { return "controlled EFI model"; }
static const char *get_dtb_path(void) { return service.external ? "external.dtb" : ""; }
static const char *get_initrd_path(void) { return ""; }
static const char *get_rndseed_path(void) { return ""; }
static int efi_md_prepare_boot(const char *name, const char *args, u_long *marks)
{
	(void)name; (void)args; (void)marks; return service.md_error;
}
static void load_modules(const char *name) { (void)name; }
static void efi_fdt_initrd(u_long a, u_long n) { (void)a; (void)n; }
static void efi_fdt_rndseed(u_long a, u_long n) { (void)a; (void)n; }
static void efi_fdt_efirng(u_long a, u_long n) { (void)a; (void)n; }
static void efi_fdt_bootargs(const char *s) { (void)s; }
static void efi_fdt_userconf(void) { }
static void efi_fdt_system_table(void) { }
static void efi_fdt_memory_map(void) { }
static void dtoverlay_foreach(void (*fn)(const char *)) { if (service.overlay) fn("controlled.dtbo"); }
/* Each intercepted FDT operation calls the real implementation unless the
 * selected service call fails. The source function and wrapper order stay real. */
int actual_fdt_setprop(void *, int, const char *, const void *, int);
int actual_fdt_appendprop(void *, int, const char *, const void *, int);
int actual_fdt_add_subnode(void *, int, const char *);
int actual_fdt_pack(void *);
static bool fdt_error_now(void)
{
	if (!tracking) return false;
	service.fdt_calls++; return service.fail_fdt != 0 && service.fdt_calls == service.fail_fdt;
}
int fdt_setprop(void *f, int n, const char *key, const void *v, int size)
{
	if (fdt_error_now()) return -FDT_ERR_NOSPACE;
	if (tracking && strcmp(key, BOOTFB_SOURCE_PROPERTY) == 0) {
		CHECK(phase == 2); event("record");
	}
	return actual_fdt_setprop(f, n, key, v, size);
}
int fdt_appendprop(void *f, int n, const char *key, const void *v, int size)
{
	if (fdt_error_now()) return -FDT_ERR_NOSPACE;
	return actual_fdt_appendprop(f, n, key, v, size);
}
int fdt_add_subnode(void *f, int n, const char *key)
{
	if (fdt_error_now()) return -FDT_ERR_NOSPACE;
	return actual_fdt_add_subnode(f, n, key);
}
int fdt_pack(void *f)
{
	if (tracking) { CHECK(phase == 2); event("pack"); }
	if (tracking && service.pack_error) return -FDT_ERR_NOSPACE;
	return actual_fdt_pack(f);
}
#include "producer.inc"
static EFI_STATUS set_virtual(UINTN size, UINTN descriptor_size, UINT32 version, void *map)
{
	CHECK(phase == 1 && size == 3 * sizeof(memory[0]));
	CHECK(descriptor_size == sizeof(memory[0]) && version == 1 && map != NULL);
	CHECK(fdt_getprop(fdt_data, fdt_path_offset(fdt_data, "/chosen"), BOOTFB_SOURCE_PROPERTY, NULL) == NULL);
	CHECK(fdt_getprop(fdt_data, fdt_path_offset(fdt_data, "/chosen"), "netbsd,uefi-runtime-mmio", NULL) != NULL || service.fail_fdt != 0);
	service.runtime_calls++; phase = 2; event("runtime");
	if (service.runtime_fb) {
		int node = fdt_add_subnode(fdt_data, 0, "runtime-framebuffer"); CHECK(node >= 0);
		CHECK(fdt_setprop_string(fdt_data, node, "compatible", "simple-framebuffer") == 0);
	}
	return service.runtime;
}
static int load_file(const char *path, u_long flags, bool noisy, EFI_PHYSICAL_ADDRESS *address, u_long *size)
{
	(void)flags; (void)noisy; *address = 0; *size = 0;
	if (strcmp(path, "controlled.dtbo") == 0) {
		if (service.overlay_load_error) return EIO;
		CHECK(fdt_create_empty_tree(overlay_tree, sizeof(overlay_tree)) == 0);
		CHECK(fdt_setprop_string(overlay_tree, 0, "compatible", "netbsd,generic-acpi") == 0);
		*address = (uintptr_t)overlay_tree; *size = sizeof(overlay_tree);
	}
	return 0;
}
static void reset_producer(void)
{
	tracking = false; panic_expected = false;
	for (unsigned i = 0; i < allocation_count; i++) free(allocations[i]);
	allocation_count = 0;
	memset(&service, 0, sizeof(service));
	service.locate = EFI_NOT_FOUND;
	memset(&efi_bootfb_source, 0, sizeof(efi_bootfb_source));
	fdt_data = NULL; fdt_data_size = 512 * 1024;
	initrd_addr = dtb_addr = rndseed_addr = 0;
	initrd_size = dtb_size = rndseed_size = 0;
	memset(memory, 0, sizeof(memory));
	for (unsigned i = 0; i < 3; i++) memory[i] = (EFI_MEMORY_DESCRIPTOR){ (uint32_t[]){5, 6, 11}[i], 0x10000 + i * 0x1000, 0, 1, EFI_MEMORY_RUNTIME };
	info = (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION){1920,1080,0,1920};
	mode = (EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE){&info,0,8294400};
	gop.Mode = &mode; phase = 0; nevents = 0; dtoverlay_enabled = true;
}
static int run_producer(struct bootfb_source_record *out)
{
	u_long marks[1] = { (u_long)(uintptr_t)final_tree };
	info.PixelFormat = service.format; mode.FrameBufferBase = service.pa;
	mode.Info = service.null_info ? NULL : &info;
	gop.Mode = service.null_mode ? NULL : &mode;
	tracking = true;
	int error = efi_fdt_prepare_boot("netbsd", "", marks);
	tracking = false;
	if (error != 0) return error;
	return bootfb_source_query(fdt_data, efi_bootfb_source.capacity, out);
}
static void producer_tests(void)
{
	struct bootfb_source_record value;
	reset_producer(); CHECK(run_producer(&value) == 0);
	CHECK(value.producer == 1 && value.disposition == 3 && value.status == EFI_NOT_FOUND && value.count == 0);
	CHECK(efi_bootfb_source.packed_valid && service.locate_calls == 1 && service.ebs_calls == 1 && service.runtime_calls == 1);
	CHECK(nevents == 6 && strcmp(events[0], "GOP") == 0 && strcmp(events[1], "EBS") == 0 && strcmp(events[2], "runtime") == 0 && strcmp(events[3], "cleanup") == 0 && strcmp(events[4], "record") == 0 && strcmp(events[5], "pack") == 0);
	int calls = service.fdt_calls; CHECK(calls > 15);
	for (unsigned status = 0; status <= 35; status++) {
		reset_producer(); service.locate = status == 0 ? 0 : EFIERR(status); service.count = 7;
		CHECK(run_producer(&value) == 0);
		CHECK(value.status == service.locate && value.count == (status == 0 ? 7U : 0U));
		CHECK(value.disposition == (status == 0 ? 1U : status == 14 ? 3U : 0U));
	}
	for (unsigned warning = 1; warning <= 5; warning++) {
	reset_producer(); service.locate = warning; service.count = 1;
	CHECK(run_producer(&value) == 0 && value.status == warning && value.count == 1 && value.disposition == 0);
}
for (unsigned count = 0; count <= 3; count++) {
		for (unsigned format = 0; format <= 3; format++) {
			reset_producer(); service.locate = 0; service.count = count; service.format = format;
			CHECK(run_producer(&value) == 0 && value.count == count);
			CHECK(value.disposition == (count == 0 ? 0U : format < 2 ? 1U : 2U));
			if (count && format != 3) {
				int node = fdt_path_offset(fdt_data, "/chosen/framebuffer@0"); CHECK(node >= 0);
				int size; const fdt64_t *reg = fdt_getprop(fdt_data, node, "reg", &size);
				CHECK(size == 16 && fdt64_to_cpu(reg[0]) == 0); /* PA0 is present. */
			}
		}
	}
	for (unsigned failure = 0; failure < 4; failure++) {
		reset_producer(); service.locate = 0; service.count = 1;
		if (failure == 0) service.protocol = EFI_DEVICE_ERROR;
		if (failure == 1) service.null_gop = true;
		if (failure == 2) service.null_mode = true;
		if (failure == 3) service.null_info = true;
		CHECK(run_producer(&value) == 0 && value.disposition == 0 && efi_bootfb_source.protocol_failed);
	}
	for (unsigned failure = 0; failure < 6; failure++) {
		reset_producer();
		if (failure == 0) service.external = true;
		if (failure == 1) service.overlay = true;
		if (failure == 2) { service.overlay = true; service.overlay_load_error = true; }
		if (failure == 3) service.no_map = true;
		if (failure == 4) service.ebs = EFI_DEVICE_ERROR;
		if (failure == 5) service.runtime = EFI_DEVICE_ERROR;
		/* Cleanup failure does not hand authority to an unknown source. */
		if (failure >= 3 && failure <= 4) {
			/* No runtime took place: the producer still writes UNKNOWN later. */
			phase = 0;
		}
		CHECK(run_producer(&value) == 0 && value.disposition == 0);
	}
	reset_producer(); service.runtime_fb = true;
	CHECK(run_producer(&value) == 0 && value.disposition == 0);
	for (int n = 1; n <= calls; n++) {
		reset_producer(); service.fail_fdt = n;
		int error = run_producer(&value);
		CHECK(error != 0 || value.disposition != 3);
	}
	/* A copied canonical record is neutralized by the actual finalizer. */
reset_producer(); create_tree(); set_record(absent_wire, 32);
CHECK(efi_fdt_set_data(tree) == 0);
struct efi_cleanup_result cleanup = { .map_available = true, .ebs_attempted = true, .runtime_attempted = true };
efi_bootfb_source.blob = fdt_data; efi_bootfb_source.capacity = fdt_data_size;
efi_bootfb_source.status = EFI_NOT_FOUND; efi_bootfb_source.gop_attempted = true;
efi_bootfb_source.disposition = 3; phase = 2;
efi_fdt_source_finish(&cleanup); efi_fdt_fini(); efi_fdt_source_packed();
CHECK(bootfb_source_query(fdt_data, fdt_data_size, &value) == 0 && value.producer == 0 && value.disposition == 0);
/* Neutralization survives a later failure to replace the copied property. */
reset_producer(); create_tree(); set_record(absent_wire, 32);
CHECK(efi_fdt_set_data(tree) == 0);
efi_bootfb_source.blob = fdt_data; efi_bootfb_source.capacity = fdt_data_size;
efi_bootfb_source.status = EFI_NOT_FOUND; efi_bootfb_source.gop_attempted = true;
efi_bootfb_source.disposition = 3; phase = 2; service.fail_fdt = 1; tracking = true;
efi_fdt_source_finish(&cleanup); tracking = false;
CHECK(efi_bootfb_source.final_error != 0);
CHECK(bootfb_source_query(fdt_data, fdt_data_size, &value) == 0 && value.disposition == 0);
/* The same write-failure sweep also covers actual GOP property publication. */
reset_producer(); service.locate = 0; service.count = 1;
CHECK(run_producer(&value) == 0 && value.disposition == 1);
int gop_calls = service.fdt_calls;
for (int n = 1; n <= gop_calls; n++) {
	reset_producer(); service.locate = 0; service.count = 1; service.fail_fdt = n;
	int error = run_producer(&value);
	CHECK(error != 0 || value.disposition != 1);
}
for (int n = 1; n <= 2; n++) {
	reset_producer(); service.fail_alloc = n;
	CHECK(run_producer(&value) == ENOMEM && service.locate_calls == 0);
}
for (unsigned failure = 0; failure < 2; failure++) {
	reset_producer(); panic_expected = true;
	if (failure == 0) service.fail_alloc = 3;
	else service.pack_error = true;
	if (setjmp(panic_jump) == 0) {
		(void)run_producer(&value); CHECK(false);
	}
	CHECK(!efi_bootfb_source.packed_valid);
	CHECK(failure == 0 ? service.runtime_calls == 0 : service.runtime_calls == 1);
}
reset_producer(); service.no_root = true; CHECK(run_producer(&value) == EINVAL && service.locate_calls == 0);
	reset_producer(); service.md_error = EIO; CHECK(run_producer(&value) == EIO && service.locate_calls == 0);
	reset_producer();
}

int main(void)
{
	#ifdef BOOTFB_SOURCE_LATE_CASE
	late_property_test(BOOTFB_SOURCE_LATE_CASE);
#else
	wire_tests(); tree_tests(); malformed_tree_tests(); mixed_chosen_tests();
	late_property_test(0); late_property_test(1); named_root_test();
	producer_tests();
#endif
	printf("boot framebuffer source contract: %u PASS\n", checks);
	return 0;
}
