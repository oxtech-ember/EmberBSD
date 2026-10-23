/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Serial HPD ownership and error fixture. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
static unsigned checks;
static void check(bool ok, const char *text, int line)
{ checks++; if (!ok) { fprintf(stderr, "%d: %s\n", line, text); exit(1); } }
#define CHECK(c) check((c), #c, __LINE__)
typedef struct { bool held; } kmutex_t;
static void mutex_enter(kmutex_t *m) { CHECK(!m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { CHECK(m->held); m->held = false; }
static unsigned reads;
static int ready_error, read_error;
static uint8_t hpd;
struct sun60i_a733_hdmi_softc {
    const void *owner;
    bool hdmi_ready;
    int fault, de0, bst, hdmi;
};
static int sun60i_a733_ccu_display_ready(int node, const void *owner)
{ CHECK(node == 2 && owner != NULL); return ready_error; }
static int bus_space_peek_1(int bst, int handle, unsigned reg, uint8_t *out)
{ CHECK(bst == 3 && handle == 4 && reg == 0x3004); reads++;
  if (read_error) return read_error;
  *out = hpd; return 0; }
/* native_detect */
static kmutex_t output_lock;
static bool output_prepared;
static int output_once, output_error;
static const void *output_owner;
static struct sun60i_a733_hdmi_softc hdmi;
static void *output_arg = &hdmi;
static struct { int (*detect)(void *, const void *, bool *); } ops;
static const __typeof__(ops) *output_ops = &ops;
static int output_init(void) { return 0; }
#define RUN_ONCE(o, f) do { if (!*(o)) { CHECK(f() == 0); *(o) = 1; } } while (0)
static int output_lookup(int node, const void *owner)
{ CHECK(output_lock.held); if (node != 7) return ENXIO;
  if (owner == NULL) return EINVAL;
  if (owner != output_owner) return EBUSY;
  return output_error; }
/* sun60i_a733_output_detect */
struct sun60i_a733_de352_client { const void *owner; } client;
struct sun60i_de352_softc { kmutex_t lock; int output_phandle; } de;
static struct sun60i_de352_softc *de352_lookup(int node,
    struct sun60i_a733_de352_client *candidate)
{ CHECK(node == 0); return candidate == &client ? &de : NULL; }
/* sun60i_a733_de352_detect */
struct a733_drm { kmutex_t lock; int fault;
    struct sun60i_a733_de352_client *client; } kms;
struct drm_device { void *dev_private; } dev = { &kms };
struct drm_connector { struct drm_device *dev; } connector = { &dev };
enum drm_connector_status { connector_status_unknown,
    connector_status_connected, connector_status_disconnected };
/* a733_detect */
int main(void)
{
    bool value = true;
    de.output_phandle = 7; client.owner = &kms; kms.client = &client;
    output_owner = &de; output_prepared = true; ops.detect = native_detect;
    hdmi = (struct sun60i_a733_hdmi_softc){ &de, true, 0, 2, 3, 4 };
    hpd = 0;
    CHECK(a733_detect(&connector, false) == connector_status_disconnected);
    hpd = 2;
    CHECK(a733_detect(&connector, false) == connector_status_connected);
    hpd = 0xfd; /* Other PHY status bits do not imply HPD. */
    CHECK(a733_detect(&connector, true) == connector_status_disconnected);
    read_error = EIO;
    CHECK(a733_detect(&connector, false) == connector_status_unknown);
    CHECK(sun60i_a733_output_detect(7, &de, &value) == EFAULT && value);
    CHECK(output_error == 0 && hdmi.fault == 0 && kms.fault == 0);
    read_error = 0; hpd = 2;
    CHECK(a733_detect(&connector, false) == connector_status_connected);
    ready_error = EBUSY; reads = 0;
    CHECK(a733_detect(&connector, false) == connector_status_unknown && reads == 0);
    ready_error = 0;
    output_prepared = false;
    CHECK(a733_detect(&connector, false) == connector_status_unknown && reads == 0);
    output_prepared = true; ops.detect = NULL;
    CHECK(sun60i_a733_output_detect(7, &de, &value) == EOPNOTSUPP && value);
    ops.detect = native_detect;
    CHECK(sun60i_a733_output_detect(8, &de, &value) == ENXIO && value);
    CHECK(sun60i_a733_output_detect(7, &kms, &value) == EBUSY && value);
    CHECK(sun60i_a733_output_detect(7, NULL, &value) == EINVAL && value);
    CHECK(sun60i_a733_output_detect(7, &de, NULL) == EINVAL);
    CHECK(sun60i_a733_de352_detect(NULL, &value) == EINVAL && value);
    CHECK(sun60i_a733_de352_detect(&client, NULL) == EINVAL);
    client.owner = NULL;
    CHECK(sun60i_a733_de352_detect(&client, &value) == EINVAL && value);
    client.owner = &kms;
    CHECK(native_detect(&hdmi, &de, NULL) == EINVAL);
    CHECK(native_detect(&hdmi, NULL, &value) == EACCES && value);
    hdmi.hdmi_ready = false;
    CHECK(native_detect(&hdmi, &de, &value) == EACCES && value);
    hdmi.hdmi_ready = true; hdmi.fault = EIO;
    CHECK(native_detect(&hdmi, &de, &value) == EIO && value);
    hdmi.fault = 0; kms.fault = EIO;
    CHECK(a733_detect(&connector, false) == connector_status_unknown && reads == 0);
    kms.fault = 0; hpd = 0;
    CHECK(sun60i_a733_output_detect(7, &de, &value) == 0 && !value);
    CHECK(!output_lock.held && !de.lock.held && !kms.lock.held);
    printf("PASS: %u actual HPD provider-chain checks\n", checks);
    return 0;
}
