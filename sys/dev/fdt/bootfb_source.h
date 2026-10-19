/* Origin: EmberBSD bounded boot framebuffer source facts. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_FDT_BOOTFB_SOURCE_H_
#define _DEV_FDT_BOOTFB_SOURCE_H_

#include <sys/types.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <libfdt.h>

#define BOOTFB_SOURCE_PROPERTY "ember,bootfb-source-v1"
#define BOOTFB_SOURCE_VERSION 1U
#define BOOTFB_SOURCE_SIZE 32U
#define BOOTFB_SOURCE_UNKNOWN 0U
#define BOOTFB_SOURCE_EFI_ACPI64 1U
#define BOOTFB_SOURCE_UNCLASSIFIED 0U
#define BOOTFB_SOURCE_PRESENT 1U
#define BOOTFB_SOURCE_UNSUPPORTED 2U
#define BOOTFB_SOURCE_ABSENT 3U
#define BOOTFB_SOURCE_EFI_NOT_FOUND UINT64_C(0x800000000000000e)

/* Values describe a trusted boot-source result. They grant no device access. */
struct bootfb_source_record {
	uint32_t producer;
	uint32_t disposition;
	uint64_t status;
	uint64_t count;
};

struct bootfb_source_tree {
	struct bootfb_source_record record;
	int chosen;
	bool framebuffer;
	bool record_present;
};

static inline int
bootfb_source_record_valid(const struct bootfb_source_record *value)
{

	if (value == NULL || value->producer > BOOTFB_SOURCE_EFI_ACPI64 ||
	    value->disposition > BOOTFB_SOURCE_ABSENT)
		return EINVAL;
	if (value->disposition == BOOTFB_SOURCE_ABSENT &&
	    (value->producer != BOOTFB_SOURCE_EFI_ACPI64 ||
	    value->status != BOOTFB_SOURCE_EFI_NOT_FOUND || value->count != 0))
		return EINVAL;
	if ((value->disposition == BOOTFB_SOURCE_PRESENT ||
	    value->disposition == BOOTFB_SOURCE_UNSUPPORTED) &&
	    (value->status != 0 || value->count == 0))
		return EINVAL;
	return 0;
}

/* The decoder requires the exact wire length. Errors do not change output. */
static inline int
bootfb_source_decode(const void *data, size_t length,
    struct bootfb_source_record *out)
{
	const uint8_t *bytes = data;
	struct bootfb_source_record value;
	int error;

	if (data == NULL || out == NULL || length != BOOTFB_SOURCE_SIZE)
		return EINVAL;
	if (be32dec(bytes) != BOOTFB_SOURCE_VERSION || be32dec(bytes + 12) != 0)
		return EINVAL;
	value.producer = be32dec(bytes + 4);
	value.disposition = be32dec(bytes + 8);
	value.status = be64dec(bytes + 16);
	value.count = be64dec(bytes + 24);
	error = bootfb_source_record_valid(&value);
	if (error != 0)
		return error;
	*out = value;
	return 0;
}

/* Larger caller buffers are accepted. Only the fixed record bytes change. */
static inline int
bootfb_source_encode(const struct bootfb_source_record *value, void *out,
    size_t capacity)
{
	uint8_t *bytes = out;
	int error;

	if (out == NULL || capacity < BOOTFB_SOURCE_SIZE)
		return EINVAL;
	error = bootfb_source_record_valid(value);
	if (error != 0)
		return error;
	be32enc(bytes, BOOTFB_SOURCE_VERSION);
	be32enc(bytes + 4, value->producer);
	be32enc(bytes + 8, value->disposition);
	be32enc(bytes + 12, 0);
	be64enc(bytes + 16, value->status);
	be64enc(bytes + 24, value->count);
	return 0;
}

/* libfdt validates bounds; this fixed producer also requires one complete root. */
static inline int
bootfb_source_shape(const void *fdt)
{
	const char *root_name;
	int offset = 0, next, depth = 0, roots = 0, root_length;
	uint32_t tag, size;
	bool properties_allowed = false;

	/* The selected producer emits the current v17 structure-size field. */
	if (fdt_version(fdt) != 17)
		return EINVAL;
	size = fdt_size_dt_struct(fdt);
	while ((uint32_t)offset < size) {
		tag = fdt_next_tag(fdt, offset, &next);
		if (next <= offset || (uint32_t)next > size)
			return EINVAL;
		switch (tag) {
		case FDT_BEGIN_NODE:
			if (depth == 0) {
				if (++roots != 1 || offset != 0)
					return EINVAL;
				root_name = fdt_get_name(fdt, offset, &root_length);
				if (root_name == NULL || root_length != 0)
					return EINVAL;
			}
			depth++;
			properties_allowed = true;
			break;
		case FDT_END_NODE:
			if (--depth < 0)
				return EINVAL;
			properties_allowed = false;
			break;
		case FDT_PROP:
			if (depth == 0 || !properties_allowed)
				return EINVAL;
			break;
		case FDT_NOP:
			break;
		case FDT_END:
			return depth == 0 && roots == 1 && (uint32_t)next == size ?
			    0 : EINVAL;
		default:
			return EINVAL;
		}
		offset = next;
	}
	return EINVAL;
}

/* The caller supplies actual readable capacity, not encoded totalsize alone. */

static inline int
bootfb_source_scan(const void *fdt, size_t capacity,
    struct bootfb_source_tree *out)
{
	struct bootfb_source_tree value = {0};
	const void *data;
	const char *name, *property;
	int node, depth = -1, length, offset, chosen_count = 0, record_count = 0;
	int compatible_count, strings, error, index;

	if (fdt == NULL || out == NULL || capacity < sizeof(struct fdt_header))
		return EINVAL;
	if (fdt_check_full(fdt, capacity) != 0 ||
	    fdt_totalsize(fdt) > capacity || bootfb_source_shape(fdt) != 0 ||
	    fdt_path_offset(fdt, "/") != 0)
		return EINVAL;
	value.chosen = fdt_path_offset(fdt, "/chosen");
	if (value.chosen < 0 ||
	    fdt_node_check_compatible(fdt, 0, "netbsd,generic-acpi") != 0)
		return EINVAL;
	/* libfdt path lookup also matches a unit-address alias. */
	name = fdt_get_name(fdt, value.chosen, &length);
	if (name == NULL || length != 6 || strcmp(name, "chosen") != 0)
		return EINVAL;
	for (node = fdt_next_node(fdt, -1, &depth); node >= 0 && depth >= 0;
	    node = fdt_next_node(fdt, node, &depth)) {
		name = fdt_get_name(fdt, node, &length);
		if (name == NULL || length < 0)
			return EINVAL;
		if (depth == 1 && length > 6 &&
		    strncmp(name, "chosen@", 7) == 0)
			return EINVAL;
		if (depth == 1 && length == 6 &&
		    name[0] == 'c' && name[1] == 'h' && name[2] == 'o' &&
		    name[3] == 's' && name[4] == 'e' && name[5] == 'n')
			chosen_count++;
		compatible_count = 0;
		fdt_for_each_property_offset(offset, fdt, node) {
			data = fdt_getprop_by_offset(fdt, offset, &property, &length);
			if (data == NULL || property == NULL || length < 0)
				return EINVAL;
			if (strcmp(property, "compatible") == 0)
				compatible_count++;
			if (strcmp(property, BOOTFB_SOURCE_PROPERTY) != 0)
				continue;
			if (node != value.chosen || depth != 1 || ++record_count != 1)
				return EINVAL;
			error = bootfb_source_decode(data, (size_t)length,
			    &value.record);
			if (error != 0)
				return error;
			value.record_present = true;
		}
		if (offset != -FDT_ERR_NOTFOUND || compatible_count > 1)
			return EINVAL;
		if (compatible_count == 0)
			continue;
		strings = fdt_stringlist_count(fdt, node, "compatible");
		if (strings <= 0)
			return EINVAL;
		for (index = 0; index < strings; index++) {
			name = fdt_stringlist_get(fdt, node, "compatible",
			    index, &length);
			if (name == NULL || length <= 0)
				return EINVAL;
		}
		if (fdt_stringlist_search(fdt, node, "compatible",
		    "simple-framebuffer") >= 0)
			value.framebuffer = true;
	}
	if (chosen_count != 1 || (node < 0 && node != -FDT_ERR_NOTFOUND))
		return EINVAL;
	if (value.record_present &&
	    value.record.disposition == BOOTFB_SOURCE_ABSENT && value.framebuffer)
		return EINVAL;
	*out = value;
	return 0;
}

static inline int
bootfb_source_query(const void *fdt, size_t capacity,
    struct bootfb_source_record *out)
{
	struct bootfb_source_tree tree;
	int error;

	if (out == NULL)
		return EINVAL;
	error = bootfb_source_scan(fdt, capacity, &tree);
	if (error != 0)
		return error;
	if (!tree.record_present)
		return ENOENT;
	*out = tree.record;
	return 0;
}
#endif
