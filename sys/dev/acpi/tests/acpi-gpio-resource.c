/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted contract fixture. It does not access ACPI firmware or GPIO hardware. */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int u_int;
typedef int ACPI_STATUS;
typedef void *ACPI_HANDLE;

#define AE_OK 0
#define AE_ERROR 1
#define AE_NOT_FOUND 2
#define AE_NO_HANDLER 3
#define AE_SUPPORT 4
#define AE_BAD_PARAMETER 5
#define AE_CTRL_TERMINATE 0x4001
#define ACPI_FAILURE(status) ((status) != AE_OK)
#define ACPI_RESOURCE_TYPE_GPIO 1
#define ACPI_RESOURCE_GPIO_TYPE_INT 0
#define ACPI_RESOURCE_GPIO_TYPE_IO 1
#define ACPI_LEVEL_SENSITIVE 0
#define ACPI_EDGE_SENSITIVE 1
#define ACPI_ACTIVE_HIGH 0
#define ACPI_ACTIVE_LOW 1
#define ACPI_ACTIVE_BOTH 2
#define GPIO_INTR_HIGH_LEVEL 1
#define GPIO_INTR_LOW_LEVEL 2
#define GPIO_INTR_NEG_EDGE 3
#define GPIO_INTR_POS_EDGE 4
#define GPIO_INTR_DOUBLE_EDGE 5
#define KASSERT(expression) assert(expression)
#define FIXTURE_ARRAYCOUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef struct acpi_resource_source {
	char *StringPtr;
} ACPI_RESOURCE_SOURCE;

typedef struct acpi_resource_gpio {
	uint8_t ConnectionType;
	uint8_t Triggering;
	uint8_t Polarity;
	uint16_t PinTableLength;
	uint16_t *PinTable;
	ACPI_RESOURCE_SOURCE ResourceSource;
} ACPI_RESOURCE_GPIO;

typedef struct acpi_resource {
	uint32_t Type;
	union {
		ACPI_RESOURCE_GPIO Gpio;
	} Data;
} ACPI_RESOURCE;

struct acpi_devnode;
struct acpi_devnode_link {
	struct acpi_devnode *sqe_next;
};
struct acpi_devnode {
	ACPI_HANDLE ad_handle;
	void *ad_gpiodev;
	int (*ad_gpio_translate)(void *, ACPI_RESOURCE_GPIO *, void **);
	void *ad_gpio_priv;
	struct acpi_devnode_link ad_list;
};
struct acpi_devnode_queue {
	struct acpi_devnode *first;
};
struct acpi_softc {
	struct acpi_devnode_queue sc_head;
};
static struct acpi_softc fixture_acpi_softc;
static struct acpi_softc *acpi_softc = &fixture_acpi_softc;

@PRODUCTION_CONTEXT@

#define SIMPLEQ_FOREACH(var, head, field) \
	for ((var) = (head)->first; (var) != NULL; (var) = (var)->field.sqe_next)

typedef ACPI_STATUS (*ACPI_WALK_RESOURCE_CALLBACK)(ACPI_RESOURCE *, void *);

struct fixture_buffer {
	ACPI_RESOURCE resources[8];
	char sources[8][12];
	uint16_t pins[8];
};

static int fixture_walk_active;
static int fixture_provider_calls;
static int fixture_bad_source_index = -1;
static int fixture_walk_error;
static int fixture_controller;
static int fixture_handle;
static int fixture_gpio_device;
#ifdef FIXTURE_RETAIN_RESOURCES
static struct fixture_buffer *fixture_retained[32];
static size_t fixture_retained_count;
#endif

const char *
AcpiFormatException(ACPI_STATUS status)
{
	(void)status;
	return "fixture ACPI error";
}

static ACPI_STATUS
AcpiGetHandle(ACPI_HANDLE parent, const char *path, ACPI_HANDLE *handle)
{
	(void)parent;
	if (!fixture_walk_active) {
		fprintf(stderr, "FAIL: namespace lookup escaped the resource callback\n");
		return AE_BAD_PARAMETER;
	}
	if (strcmp(path, "\\_SB.GPO0") != 0) {
		return AE_NOT_FOUND;
	}
	*handle = &fixture_handle;
	return AE_OK;
}

static int
fixture_gpio_translate(void *priv, ACPI_RESOURCE_GPIO *resource, void **gpiop)
{
	(void)priv;
	if (!fixture_walk_active) {
		fprintf(stderr, "FAIL: provider read escaped the resource callback\n");
		return -1;
	}
	fixture_provider_calls++;
	if (resource->ResourceSource.StringPtr == NULL ||
	    strcmp(resource->ResourceSource.StringPtr, "\\_SB.GPO0") != 0 ||
	    resource->PinTableLength != 1 || resource->PinTable == NULL) {
		fprintf(stderr, "FAIL: nested resource fields are invalid\n");
		return -1;
	}
	*gpiop = &fixture_controller;
	return resource->PinTable[0];
}

static ACPI_STATUS
AcpiWalkResources(ACPI_HANDLE handle, const char *method,
	ACPI_WALK_RESOURCE_CALLBACK callback, void *context)
{
	struct fixture_buffer *buffer;
	ACPI_STATUS status = AE_OK;
	size_t i;

	(void)handle;
	if (strcmp(method, "_CRS") != 0) {
		return AE_BAD_PARAMETER;
	}
	if (fixture_walk_error != AE_OK) {
		return fixture_walk_error;
	}
	buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return AE_ERROR;
	}
	buffer->resources[0].Type = 0;
	for (i = 1; i < 8; i++) {
		ACPI_RESOURCE_GPIO *gpio = &buffer->resources[i].Data.Gpio;
		buffer->resources[i].Type = ACPI_RESOURCE_TYPE_GPIO;
		gpio->ConnectionType = (i == 2 || i == 4) ?
		    ACPI_RESOURCE_GPIO_TYPE_IO : ACPI_RESOURCE_GPIO_TYPE_INT;
		gpio->Triggering = ACPI_LEVEL_SENSITIVE;
		gpio->Polarity = ACPI_ACTIVE_HIGH;
		if (i == 3) {
			gpio->Triggering = ACPI_EDGE_SENSITIVE;
			gpio->Polarity = ACPI_ACTIVE_LOW;
		} else if (i == 5) {
			gpio->Triggering = ACPI_EDGE_SENSITIVE;
			gpio->Polarity = ACPI_ACTIVE_BOTH;
		} else if (i == 6) {
			gpio->Polarity = ACPI_ACTIVE_LOW;
		} else if (i == 7) {
			gpio->Triggering = ACPI_EDGE_SENSITIVE;
			gpio->Polarity = ACPI_ACTIVE_HIGH;
		}
		(void)snprintf(buffer->sources[i], sizeof(buffer->sources[i]),
		    "\\_SB.GPO0");
		if ((int)i == fixture_bad_source_index) {
			(void)snprintf(buffer->sources[i], sizeof(buffer->sources[i]),
			    "\\_SB.BAD0");
		}
		buffer->pins[i] = (uint16_t)(16 + i);
		gpio->PinTableLength = 1;
		gpio->PinTable = &buffer->pins[i];
		gpio->ResourceSource.StringPtr = buffer->sources[i];
	}

	fixture_walk_active = 1;
	for (i = 0; i < 8; i++) {
		status = callback(&buffer->resources[i], context);
		if (status == AE_CTRL_TERMINATE) {
			status = AE_OK;
			break;
		}
		if (ACPI_FAILURE(status)) {
			break;
		}
	}
	fixture_walk_active = 0;
#ifdef FIXTURE_RETAIN_RESOURCES
	if (fixture_retained_count == FIXTURE_ARRAYCOUNT(fixture_retained)) {
		free(buffer);
		return AE_ERROR;
	}
	fixture_retained[fixture_retained_count++] = buffer;
#else
	memset(buffer, 0xa5, sizeof(*buffer));
	free(buffer);
#endif
	return status;
}

static void
fixture_cleanup(void)
{
#ifdef FIXTURE_RETAIN_RESOURCES
	size_t i;

	for (i = 0; i < fixture_retained_count; i++) {
		free(fixture_retained[i]);
	}
#endif
}

@PRODUCTION_FUNCTIONS@

static struct acpi_devnode fixture_gpio_node = {
	.ad_handle = &fixture_handle,
	.ad_gpiodev = &fixture_gpio_device,
	.ad_gpio_translate = fixture_gpio_translate,
};

static int
check_int(u_int index, int expected_pin, int expected_mode)
{
	void *gpiop = NULL;
	int pin = -1;
	int irqmode = -1;
	ACPI_STATUS status;

	status = acpi_gpio_get_int(NULL, index, &gpiop, &pin, &irqmode);
	if (status != AE_OK || gpiop != &fixture_controller ||
	    pin != expected_pin || irqmode != expected_mode) {
		fprintf(stderr, "FAIL: interrupt resource %u: status=%d pin=%d mode=%d\n",
		    index, status, pin, irqmode);
		return 1;
	}
	return 0;
}

static int
check_io(u_int index, int expected_pin)
{
	void *gpiop = NULL;
	int pin = -1;
	ACPI_STATUS status;

	status = acpi_gpio_get_io(NULL, index, &gpiop, &pin);
	if (status != AE_OK || gpiop != &fixture_controller ||
	    pin != expected_pin) {
		fprintf(stderr, "FAIL: I/O resource %u: status=%d pin=%d\n",
		    index, status, pin);
		return 1;
	}
	return 0;
}

static int
check_missing(int interrupt, u_int index, ACPI_STATUS expected)
{
	void *gpiop = (void *)(uintptr_t)0x1234;
	int pin = 1234;
	int irqmode = 1234;
	ACPI_STATUS status;

	if (interrupt) {
		status = acpi_gpio_get_int(NULL, index, &gpiop, &pin, &irqmode);
	} else {
		status = acpi_gpio_get_io(NULL, index, &gpiop, &pin);
	}
	if (status != expected || gpiop != (void *)(uintptr_t)0x1234 ||
	    pin != 1234 || (interrupt && irqmode != 1234)) {
		fprintf(stderr, "FAIL: missing resource %u: status=%d\n", index, status);
		return 1;
	}
	return 0;
}

int
main(void)
{
	atexit(fixture_cleanup);
	fixture_acpi_softc.sc_head.first = &fixture_gpio_node;
	if (check_int(0, 17, GPIO_INTR_HIGH_LEVEL) ||
	    check_int(1, 19, GPIO_INTR_NEG_EDGE) ||
	    check_int(2, 21, GPIO_INTR_DOUBLE_EDGE) ||
	    check_int(3, 22, GPIO_INTR_LOW_LEVEL) ||
	    check_int(4, 23, GPIO_INTR_POS_EDGE) ||
	    check_io(0, 18) || check_io(1, 20) ||
	    check_missing(1, 5, AE_NOT_FOUND) ||
	    check_missing(0, 2, AE_NOT_FOUND)) {
		return 1;
	}
	fixture_bad_source_index = 1;
	if (check_missing(1, 0, AE_NOT_FOUND)) {
		return 1;
	}
	fixture_bad_source_index = -1;
	fixture_gpio_node.ad_gpiodev = NULL;
	if (check_missing(1, 0, AE_NO_HANDLER)) {
		return 1;
	}
	fixture_gpio_node.ad_gpiodev = &fixture_gpio_device;
	fixture_walk_error = AE_ERROR;
	if (check_missing(0, 0, AE_ERROR)) {
		return 1;
	}
	if (fixture_provider_calls != 7) {
		fprintf(stderr, "FAIL: provider called %d times\n",
		    fixture_provider_calls);
		return 1;
	}
	puts("ACPI GPIO resource lifetime: PASS (12 cases)");
	return 0;
}
