/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual observer/clock reader/libfdt; existing controlled kernel services. */
#include "observer-services.h"

static bool intr_context, soft_context;
static unsigned int expected_reads;
static const uint64_t hvs_offsets[2][8] = {
	{ 0, 0x20, 0x30, 0x44, 0x50, 0x64, 0x70, 0x84 },
	{ 0, 0x20, 0x100, 0x118, 0x140, 0x158, 0x180, 0x198 }
};
static bool cpu_intr_p(void) { return intr_context; }
static bool cpu_softintr_p(void) { return soft_context; }
#define ASSERT_SLEEPABLE() CHECK(!intr_context && !soft_context)

static int
bcm2712_display_acpi_resources(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_display_contract *out)
{
	unsigned int i;

	CHECK(dev == &device_token && handle == dev && references == 1);
	CHECK(exchanges == 0 && maps == 0);
	resources++;
	if (resource_error != 0) return resource_error;
	memset(out, 0, sizeof(*out));
	out->version = 1;
	for (i = 0; i < 32; i++) out->memory[i] = bcm2712_display_resources[i].memory;
	if (resource_bad == 1) out->memory[3].address += 4;
	if (resource_bad == 2) out->memory[3].length += 4;
	return 0;
}
static int
bcmmbox_request(unsigned int channel, void *buffer, size_t length, uint32_t *reply)
{
	uint32_t *request = buffer, id, tag, value;
	unsigned int position = exchanges++, pass = position / 8;
	unsigned int index = (position % 8) / 2;

	CHECK(channel == 8 && length == 32 && ((uintptr_t)buffer & 15) == 0);
	CHECK(references == 1 && !mapped && position < 16);
	CHECK(pass == 0 ? maps == 0 : (unmaps == 1 && reads == expected_reads));
	id = le32toh(request[5]); tag = le32toh(request[2]);
	CHECK(id == ids[index] && tag == (position % 2 ? 0x30002U : 0x30001U));
	CHECK(le32toh(request[0]) == 32 && request[1] == 0 &&
	    le32toh(request[3]) == 8 && request[4] == 0 &&
	    request[6] == 0 && request[7] == 0);
	if ((int)position == transport_at) return ETIMEDOUT;
	value = position % 2 ? clock_rate[pass][index] : clock_state[pass][index];
	request[1] = htole32(0x80000000U);
	request[4] = htole32(0x80000008U);
	request[6] = htole32(value);
	if ((int)position == wire_at) request[5] = htole32(UINT32_MAX);
	*reply = 0;
	return 0;
}
static int
bus_space_map(struct bus_space *tag, bus_addr_t address, bus_size_t length,
    int flags, bus_space_handle_t *out)
{
	CHECK(tag == &arm_generic_bs_tag && address == UINT64_C(0x107c580000));
	CHECK(length == 0x1a000 && flags == 0 && references == 1);
	CHECK(exchanges == 8 && resources == 1 && maps == 0 && !mapped);
	maps++;
	if (map_error != 0) return map_error;
	*out = 0x4150;
	mapped = true;
	return 0;
}
static uint32_t
bus_space_read_4(struct bus_space *tag, bus_space_handle_t handle, bus_size_t offset)
{
	unsigned int position = reads++;
	unsigned int layout = (words[0][0] & 0xffU) == 0x54 ? 1 : 0;

	CHECK(tag == &arm_generic_bs_tag && handle == 0x4150 && mapped);
	CHECK(references == 1 && exchanges == 8 && position < expected_reads);
	CHECK(offset == hvs_offsets[layout][position % 8] && offset + 4 <= 0x1a000);
	return words[position / 8][position % 8];
}
static void
bus_space_unmap(struct bus_space *tag, bus_space_handle_t handle, bus_size_t length)
{
	CHECK(tag == &arm_generic_bs_tag && handle == 0x4150 && length == 0x1a000);
	CHECK(references == 1 && mapped && unmaps == 0 && exchanges == 8);
	mapped = false;
	unmaps++;
}
static void
capture(const char *format, va_list ap)
{
	int length;

	if (strstr(format, "first resource3 MMIO") == NULL)
		CHECK(references == 0 && !mapped);
	length = vsnprintf(log_text + log_size, sizeof(log_text) - log_size, format, ap);
	CHECK(length >= 0 && (size_t)length < sizeof(log_text) - log_size);
	log_size += (size_t)length;
}
static void aprint_normal(const char *format, ...)
{ va_list ap; va_start(ap, format); capture(format, ap); va_end(ap); }
static void aprint_error(const char *format, ...)
{ va_list ap; va_start(ap, format); capture(format, ap); va_end(ap); }
#include "observer.h"

static void
reset(unsigned int variant)
{
	unsigned int pass, i;

	CHECK(references == 0 && !mapped && variant < 2);
	references = acquisitions = releases = resources = maps = unmaps = reads = exchanges = 0;
	resource_error = resource_bad = map_error = 0;
	transport_at = wire_at = -1;
	find_device = right_driver = render = true;
	genfb = wsdisplay = get_handle_error = missing_node = false;
	intr_context = soft_context = false;
	early_fb = 0;
	driver.name = "v3d";
	drm = (struct drm_device){ &device_token, &driver, true, &minor };
	minor = (struct drm_minor){ DRM_MINOR_RENDER, &drm };
	node.ad_device = &device_token;
	properties = (struct properties){ "EmberBSD:accepted-observer-kernel", "observe0", NULL, true, false, false };
	CHECK(fdt_create_empty_tree(blob, sizeof(blob)) == 0);
	tree = blob;
	log_size = 0; log_text[0] = 0;
	expected_reads = 16;
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < 4; i++) {
			clock_state[pass][i] = 1;
			clock_rate[pass][i] = i == 0 ? 149985000 : 324000000;
		}
		/* Independent vectors keep unrelated geometry/configuration bits. */
		memcpy(words[pass], (uint32_t[]){ variant ? 0xa5a50054U : 0xa5a50053U,
		    0x002000f0, 0x077f0437, 0x00410000,
		    0x077f0437, 0x00420000, 0x077f0437, 0x00430000 }, sizeof(words[pass]));
	}
}
static int
run(void)
{
	int error = bcm2712_hvs_observe_modcmd(MODULE_CMD_INIT, &properties);

	CHECK(references == 0 && !mapped);
	CHECK(bcm2712_hvs_observe_modcmd(MODULE_CMD_FINI, NULL) == 0);
	CHECK(bcm2712_hvs_observe_modcmd(99, NULL) == ENOTTY);
	return error;
}
static void
zero_effects(void)
{
	CHECK(resources == 0 && maps == 0 && reads == 0 && exchanges == 0);
}
static unsigned int
occurrences(const char *text)
{
	const char *p = log_text;
	unsigned int count = 0;

	while ((p = strstr(p, text)) != NULL) { count++; p += strlen(text); }
	return count;
}
static void
completed(void)
{
	CHECK(run() == 0 && reads == 16 && exchanges == 16);
	CHECK(maps == 1 && unmaps == 1 && releases == 1);
	CHECK(occurrences("HVS registers pass") == 16);
	CHECK(occurrences("HVS clocks") == 8);
	CHECK(strstr(log_text, "snapshot complete: initial_inactive_snapshot 1") != NULL);
	CHECK(strstr(log_text, "no ownership, power, DMA or stop/drain grant") != NULL);
}
static void
not_completed(int error, unsigned int count)
{
	int actual = run();

	if (actual != error)
		fprintf(stderr, "expected observation errno %d, got %d\n", error, actual);
	CHECK(actual == error && reads == count);
	CHECK(maps == 1 && unmaps == 1 && releases == 1);
	CHECK(occurrences("HVS registers pass") == count);
	CHECK(strstr(log_text, "snapshot complete") == NULL);
	CHECK(strstr(log_text, "initial_inactive_snapshot 0") != NULL);
}
static void
post_error_logged(int error)
{
	char expected[64];

	(void)snprintf(expected, sizeof(expected), "post_error %d", error);
	CHECK(strstr(log_text, expected) != NULL);
}
static void
early_tests(void)
{
	static const char * const conflicts[] = { "clock-mode", "dvp-mode",
	    "hd-mode", "hpd-mode", "ddc-mode", "phy-mode", "iommu-mode",
	    "disp-mode", "prepare-only" };
	unsigned int i;
	int offset;

	reset(0); CHECK(bcm2712_hvs_observe_modcmd(MODULE_CMD_INIT, NULL) == EINVAL); zero_effects();
	reset(0); intr_context = true; CHECK(run() == EWOULDBLOCK); zero_effects();
	reset(0); soft_context = true; CHECK(run() == EWOULDBLOCK); zero_effects();
	reset(0); properties.cold = false; CHECK(run() == EINVAL); zero_effects();
	reset(0); properties.bad_bool = true; CHECK(run() == EINVAL); zero_effects();
	reset(0); properties.expected = NULL; CHECK(run() == EINVAL); zero_effects();
	reset(0); properties.expected = "wrong"; CHECK(run() == EPROGMISMATCH); zero_effects();
	reset(0); properties.mode = NULL; CHECK(run() == EINVAL); zero_effects();
	reset(0); properties.mode = "other"; CHECK(run() == EINVAL); zero_effects();
	reset(0); properties.bad_string = true; CHECK(run() == EINVAL); zero_effects();
	for (i = 0; i < __arraycount(conflicts); i++) {
		reset(0); properties.conflict = conflicts[i]; CHECK(run() == EINVAL); zero_effects();
	}
	reset(0); early_fb = 0x1000; CHECK(run() == EBUSY); zero_effects();
	reset(0); genfb = true; CHECK(run() == EBUSY); zero_effects();
	reset(0); wsdisplay = true; CHECK(run() == EBUSY); zero_effects();
	reset(0); tree = NULL; CHECK(run() == EIO); zero_effects();
	reset(0); blob[0] = 0; CHECK(run() == EIO); zero_effects();
	reset(0); offset = fdt_add_subnode(blob, 0, "framebuffer@0"); CHECK(offset >= 0);
	CHECK(run() == EBUSY); zero_effects();
	reset(0); offset = fdt_add_subnode(blob, 0, "fb"); CHECK(offset >= 0);
	CHECK(fdt_setprop_string(blob, offset, "compatible", "simple-framebuffer") == 0);
	CHECK(run() == EBUSY); zero_effects();
	reset(0); find_device = false; CHECK(run() == ENODEV); zero_effects();
	reset(0); right_driver = false; CHECK(run() == ENODEV); zero_effects();
	reset(0); render = false; CHECK(run() == ENODEV); zero_effects(); CHECK(releases == 0);
	reset(0); minor.type = 1; CHECK(run() == ENODEV); zero_effects(); CHECK(releases == 1);
	reset(0); drm.registered = false; CHECK(run() == ENODEV); zero_effects();
	reset(0); drm.dev = NULL; CHECK(run() == ENODEV); zero_effects();
	reset(0); drm.render = NULL; CHECK(run() == ENODEV); zero_effects();
	reset(0); driver.name = "other"; CHECK(run() == ENODEV); zero_effects();
	reset(0); get_handle_error = true; CHECK(run() == ENODEV); zero_effects();
	reset(0); missing_node = true; CHECK(run() == ENODEV); zero_effects();
	reset(0); node.ad_device = NULL; CHECK(run() == ENODEV); zero_effects();
}
static void
clock_tests(void)
{
	unsigned int position, pass, index, kind;

	for (position = 0; position < 16; position++) {
		reset(0); transport_at = (int)position;
		CHECK(run() == ETIMEDOUT && exchanges == position + 1);
		CHECK(reads == (position < 8 ? 0U : 16U));
		CHECK(maps == (position < 8 ? 0U : 1U) && unmaps == maps);
		CHECK(strstr(log_text, "snapshot complete") == NULL);
		CHECK(strstr(log_text, "validated clock failure") == NULL);
		reset(1); wire_at = (int)position; CHECK(run() == EIO);
		CHECK(exchanges == position + 1);
		CHECK(strstr(log_text, "validated clock failure") == NULL);
	}
	for (pass = 0; pass < 2; pass++) {
		for (index = 0; index < 4; index++) {
			for (kind = 0; kind < 2; kind++) {
				reset(0);
				if (kind == 0) clock_state[pass][index] = 0;
				else clock_rate[pass][index] = 0;
				CHECK(run() == EBUSY && exchanges == pass * 8 + index * 2 + 2);
				CHECK(reads == (pass == 0 ? 0U : 16U) && unmaps == maps);
				CHECK(strstr(log_text, "validated clock failure") != NULL);
				CHECK(strstr(log_text, "snapshot complete") == NULL);
			}
		}
		reset(0); clock_rate[pass][0] = 119999999;
		CHECK(run() == EBUSY && reads == (pass == 0 ? 0U : 16U));
	}
}
static void
combined_test(bool pattern)
{
	reset(0);
	if (pattern) words[0][1] = words[1][1] = 1U << 31;
	else words[1][2] ^= 1;
	wire_at = 8;
	not_completed(pattern ? EBUSY : EAGAIN, 16);
	post_error_logged(EIO);
	CHECK(exchanges == 9);
}
static void
register_tests(void)
{
	unsigned int variant, byte, bit, index, field;

	for (variant = 0; variant < 2; variant++) {
		reset(variant); completed();
		for (byte = 0; byte < 256; byte++) {
			reset(variant); words[0][0] = words[1][0] = 0xdead0000U | byte;
			if (byte == 0x53 || byte == 0x54) completed();
			else { expected_reads = 1; not_completed(EOPNOTSUPP, 1); CHECK(exchanges == 16); }
			reset(variant); words[1][0] = byte;
			if (byte == (variant == 0 ? 0x53U : 0x54U)) {
				/* Same low byte, different upper VERSION bits: all16 reads. */
				not_completed(EAGAIN, 16);
			} else { expected_reads = 9; not_completed(EAGAIN, 9); }
			CHECK(exchanges == 16);
		}
		for (bit = 8; bit < 32; bit++) {
			reset(variant); words[0][0] ^= 1U << bit; words[1][0] ^= 1U << bit; completed();
		}
		for (index = 1; index < 8; index++) {
			reset(variant); words[1][index] ^= 1; not_completed(EAGAIN, 16);
		}
		for (bit = 0; bit < 32; bit++) {
			reset(variant); words[0][1] |= 1U << bit; words[1][1] |= 1U << bit;
			if (bit == 31) not_completed(EBUSY, 16); else completed();
			for (index = 2; index < 8; index += 2) {
				reset(variant); words[0][index] |= 1U << bit; words[1][index] |= 1U << bit;
				if (bit >= 30) not_completed(EBUSY, 16); else completed();
				reset(variant); words[0][index + 1] |= 1U << bit; words[1][index + 1] |= 1U << bit;
				if (bit == 13 || bit == 14) not_completed(EBUSY, 16); else completed();
			}
		}
		for (index = 2; index < 8; index += 2) {
			for (field = 1; field < 4; field++) {
				reset(variant); words[0][index + 1] |= field << 13;
				words[1][index + 1] |= field << 13; not_completed(EBUSY, 16);
			}
		}
	}
	for (index = 0; index < 4; index++) {
		reset(0); clock_rate[1][index]++; not_completed(EAGAIN, 16);
	}
	for (index = 0; index < 3; index++) {
		reset(0); if (index == 0) resource_error = EIO; else resource_bad = (int)index;
		CHECK(run() == (index == 0 ? EIO : EINVAL));
		CHECK(exchanges == 0 && maps == 0 && releases == 1);
	}
	reset(0); map_error = ENOMEM; CHECK(run() == ENOMEM);
	CHECK(exchanges == 8 && reads == 0 && unmaps == 0 && releases == 1);
	reset(0); expected_reads = 1; words[0][0] = 0; wire_at = 8;
	not_completed(EOPNOTSUPP, 1); post_error_logged(EIO);
	reset(0); expected_reads = 9; words[1][0] = 0x54; wire_at = 8;
	not_completed(EAGAIN, 9); post_error_logged(EIO);
	combined_test(false); combined_test(true);
}
int
main(void)
{
	const char *only = getenv("HVS_OBSERVE_ONLY");

	if (only != NULL) {
		CHECK(strcmp(only, "combined-raw") == 0 || strcmp(only, "combined-pattern") == 0);
		combined_test(strcmp(only, "combined-pattern") == 0);
	} else { early_tests(); clock_tests(); register_tests(); }
	printf("HVS actual-source observer: %u checks PASS\n", checks);
	return 0;
}
