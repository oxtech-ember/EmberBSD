/* SPDX-License-Identifier: BSD-2-Clause */
/* Generic ADI converter registry; no platform or FDT attachment dependencies. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <dev/fdt/adi_axi_adc_private.h>

struct adi_axi_converter_head adi_axi_converters = LIST_HEAD_INITIALIZER(adi_axi_converters);
kmutex_t adi_axi_registry_lock;
static ONCE_DECL(registry_once);

static int
registry_init(void)
{
	mutex_init(&adi_axi_registry_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

void
adi_axi_registry_init(void)
{
	RUN_ONCE(&registry_once, registry_init);
}

int
adi_axi_converter_register(device_t dev, int phandle,
    const struct adi_axi_converter_ops *ops, void *cookie,
    struct adi_axi_converter **result)
{
	struct adi_axi_converter *c, *other;

	if (dev == NULL || phandle <= 0 || ops == NULL ||
	    ops->configure == NULL || result == NULL)
		return EINVAL;
	*result = NULL;
	RUN_ONCE(&registry_once, registry_init);
	c = kmem_zalloc(sizeof(*c), KM_SLEEP);
	c->c_dev = dev;
	c->c_phandle = phandle;
	c->c_ops = ops;
	c->c_cookie = cookie;
	mutex_init(&c->c_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_enter(&adi_axi_registry_lock);
	LIST_FOREACH(other, &adi_axi_converters, c_next) {
		if (other->c_phandle == phandle) {
			mutex_exit(&adi_axi_registry_lock);
			mutex_destroy(&c->c_lock);
			kmem_free(c, sizeof(*c));
			return EEXIST;
		}
	}
	device_acquire(dev);
	LIST_INSERT_HEAD(&adi_axi_converters, c, c_next);
	mutex_exit(&adi_axi_registry_lock);
	*result = c;
	return 0;
}

int
adi_axi_converter_unregister(struct adi_axi_converter *c)
{
	mutex_enter(&adi_axi_registry_lock);
	mutex_enter(&c->c_lock);
	if (c->c_lease != NULL || c->c_poisoned) {
		mutex_exit(&c->c_lock);
		mutex_exit(&adi_axi_registry_lock);
		return EBUSY;
	}
	LIST_REMOVE(c, c_next);
	mutex_exit(&c->c_lock);
	mutex_exit(&adi_axi_registry_lock);
	device_release(c->c_dev);
	mutex_destroy(&c->c_lock);
	kmem_free(c, sizeof(*c));
	return 0;
}

int
adi_axi_converter_control_enter(struct adi_axi_converter *c)
{
	mutex_enter(&c->c_lock);
	if (c->c_lease != NULL || c->c_poisoned) {
		int error = c->c_poisoned ? EIO : EBUSY;
		mutex_exit(&c->c_lock);
		return error;
	}
	/* General converter control consumes the never-configured admission. */
	c->c_bootstrap_used = true;
	return 0;
}

void
adi_axi_converter_control_exit(struct adi_axi_converter *c)
{
	mutex_exit(&c->c_lock);
}
