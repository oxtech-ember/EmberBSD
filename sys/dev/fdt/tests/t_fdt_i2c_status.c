/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define I2C_COOKIE_OF 1
#define I2C_MAX_ADDR 0x7f
#define CFARGS(...) 0
#define be32toh(value) __builtin_bswap32(value)
#define be64toh(value) __builtin_bswap64(value)

typedef unsigned int u_int;

struct fake_device {
	int id;
	const char *name;
	uint32_t reg_be;
	const char *status;
};

struct fake_dict {
	char name[32];
	uint32_t addr;
	uint64_t cookie;
	uint32_t cookietype;
	struct fake_array *children;
	bool no_indirect_config;
};

struct fake_array {
	struct fake_dict *entries[8];
	size_t count;
};

typedef struct fake_dict *prop_dictionary_t;
typedef struct fake_array *prop_array_t;
typedef void *prop_object_t;
typedef void *device_t;
typedef int (*cfprint_t)(void *, const char *);
typedef void *i2c_tag_t;

struct i2cbus_attach_args {
	i2c_tag_t iba_tag;
	prop_array_t iba_child_devices;
};

typedef bool (*of_i2c_node_filter_t)(int);

void of_enter_i2c_devs(prop_dictionary_t, int, size_t, int);
void of_enter_i2c_devs_filtered(prop_dictionary_t, int, size_t, int,
    of_i2c_node_filter_t);
device_t fdtbus_attach_i2cbus(device_t, int, i2c_tag_t, cfprint_t);
bool fdtbus_status_okay(int);

int OF_child(int);
int OF_peer(int);
int OF_getprop(int, const char *, void *, int);
int OF_getproplen(int, const char *);
bool of_to_dataprop(prop_dictionary_t, int, const char *, const char *);
prop_array_t prop_array_create(void);
prop_dictionary_t prop_dictionary_create(void);
void prop_dictionary_set_string(prop_dictionary_t, const char *, const char *);
void prop_dictionary_set_uint32(prop_dictionary_t, const char *, uint32_t);
void prop_dictionary_set_uint64(prop_dictionary_t, const char *, uint64_t);
void prop_dictionary_set(prop_dictionary_t, const char *, prop_object_t);
void prop_array_add(prop_array_t, prop_object_t);
void prop_object_release(prop_object_t);
void prop_object_retain(prop_object_t);
prop_object_t prop_dictionary_get(prop_dictionary_t, const char *);
void prop_dictionary_set_bool(prop_dictionary_t, const char *, bool);
prop_dictionary_t device_properties(device_t);
device_t config_found(device_t, void *, cfprint_t, int);
int of_getprop_uint32(int, const char *, uint32_t *);
void *fdtbus_get_data(void);
int fdtbus_phandle2offset(int);
const void *fdt_getprop(const void *, int, const char *, int *);
int iicbus_print(void *, const char *);

static struct fake_device nodes[4];
static struct fake_array arrays[4];
static struct fake_dict dicts[8];
static size_t next_array, next_dict;
static struct fake_dict captured_props;
static i2c_tag_t captured_tag;

int
OF_child(int parent)
{
	return parent == 100 ? 1 : 0;
}

int
OF_peer(int node)
{
	return node < 3 ? node + 1 : 0;
}

int
OF_getprop(int node, const char *name, void *buffer, int length)
{
	if (node < 1 || node > 3)
		return -1;
	if (strcmp(name, "name") == 0) {
		size_t n = strlen(nodes[node].name) + 1;
		if ((size_t)length < n)
			return -1;
		memcpy(buffer, nodes[node].name, n);
		return (int)n;
	}
	if (strcmp(name, "reg") == 0 && length >= (int)sizeof(uint32_t)) {
		memcpy(buffer, &nodes[node].reg_be, sizeof(uint32_t));
		return sizeof(uint32_t);
	}
	return -1;
}

int
OF_getproplen(int node, const char *name)
{
	return node >= 1 && node <= 3 && strcmp(name, "reg") == 0 ?
	    sizeof(uint32_t) : -1;
}

bool
of_to_dataprop(prop_dictionary_t dict, int node, const char *source,
    const char *destination)
{
	(void)dict;
	(void)node;
	(void)source;
	(void)destination;
	return true;
}

prop_array_t
prop_array_create(void)
{
	return &arrays[next_array++];
}

prop_dictionary_t
prop_dictionary_create(void)
{
	return &dicts[next_dict++];
}

void
prop_dictionary_set_string(prop_dictionary_t dict, const char *key,
    const char *value)
{
	(void)key;
	(void)snprintf(dict->name, sizeof(dict->name), "%s", value);
}

void
prop_dictionary_set_uint32(prop_dictionary_t dict, const char *key,
    uint32_t value)
{
	if (strcmp(key, "addr") == 0)
		dict->addr = value;
	else
		dict->cookietype = value;
}

void
prop_dictionary_set_uint64(prop_dictionary_t dict, const char *key,
    uint64_t value)
{
	(void)key;
	dict->cookie = value;
}

void
prop_dictionary_set(prop_dictionary_t dict, const char *key,
    prop_object_t value)
{
	(void)key;
	dict->children = value;
}

void
prop_array_add(prop_array_t array, prop_object_t value)
{
	array->entries[array->count++] = value;
}

void
prop_object_release(prop_object_t object)
{
	(void)object;
}

void
prop_object_retain(prop_object_t object)
{
	(void)object;
}

prop_object_t
prop_dictionary_get(prop_dictionary_t dict, const char *key)
{
	(void)key;
	return dict->children;
}

void
prop_dictionary_set_bool(prop_dictionary_t dict, const char *key, bool value)
{
	(void)key;
	dict->no_indirect_config = value;
}

prop_dictionary_t
device_properties(device_t dev)
{
	(void)dev;
	return &captured_props;
}

device_t
config_found(device_t dev, void *aux, cfprint_t print, int flags)
{
	struct i2cbus_attach_args *iba = aux;
	(void)dev;
	(void)print;
	(void)flags;
	captured_tag = iba->iba_tag;
	captured_props.children = iba->iba_child_devices;
	return (device_t)(uintptr_t)1;
}

int
of_getprop_uint32(int node, const char *name, uint32_t *value)
{
	(void)node;
	if (strcmp(name, "#address-cells") != 0)
		return 1;
	*value = 1;
	return 0;
}

void *
fdtbus_get_data(void)
{
	return nodes;
}

int
fdtbus_phandle2offset(int phandle)
{
	return phandle;
}

const void *
fdt_getprop(const void *fdt, int node, const char *name, int *length)
{
	const struct fake_device *device = fdt;
	(void)length;
	if (node < 1 || node > 3 || strcmp(name, "status") != 0)
		return NULL;
	return device[node].status;
}

int
iicbus_print(void *aux, const char *name)
{
	(void)aux;
	(void)name;
	return 0;
}

static void
reset_fixture(void)
{
	memset(nodes, 0, sizeof(nodes));
	memset(arrays, 0, sizeof(arrays));
	memset(dicts, 0, sizeof(dicts));
	memset(&captured_props, 0, sizeof(captured_props));
	next_array = next_dict = 0;
	nodes[1] = (struct fake_device){ 1, "camera@3c", 0x3c000000,
	    "disabled" };
	nodes[2] = (struct fake_device){ 2, "camera@68", 0x68000000,
	    "okay" };
	nodes[3] = (struct fake_device){ 3, "eeprom@50", 0x50000000,
	    NULL };
}

static int
check_children(prop_array_t array, const uint32_t *addresses, size_t count)
{
	if (array == NULL || array->count != count)
		return 1;
	for (size_t i = 0; i < count; i++)
		if (array->entries[i]->addr != addresses[i])
			return 2;
	return 0;
}

int
main(void)
{
	static const uint32_t enabled_addrs[] = { 0x68, 0x50 };
	static const uint32_t all_addrs[] = { 0x3c, 0x68, 0x50 };
	prop_dictionary_t props;
	int error;

	reset_fixture();
	fdtbus_attach_i2cbus((device_t)(uintptr_t)2, 100, NULL, iicbus_print);
	if (captured_tag != NULL)
		return 10;
	error = check_children(captured_props.children, enabled_addrs,
	    sizeof(enabled_addrs) / sizeof(enabled_addrs[0]));
	if (error != 0) {
		fprintf(stderr, "FDT filtered child check failed: %d\n", error);
		return 11;
	}

	reset_fixture();
	props = prop_dictionary_create();
	of_enter_i2c_devs(props, 100, sizeof(uint32_t), 0);
	error = check_children(props->children, all_addrs,
	    sizeof(all_addrs) / sizeof(all_addrs[0]));
	if (error != 0) {
		fprintf(stderr, "legacy OF enumeration changed: %d\n", error);
		return 12;
	}

	return 0;
}
