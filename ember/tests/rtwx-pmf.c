/* Origin: EmberBSD (AI-assisted). */
/* Run the driver registration block and common PMF network callbacks.
 * The model replaces device registration, locks and interface hardware calls.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#define IFF_UP 1
#define IFF_RUNNING 0x40
struct ifnet;
typedef int pmf_qual_t;
struct device {
    bool (*suspend)(struct device *, const pmf_qual_t *);
    bool (*resume)(struct device *, const pmf_qual_t *);
    struct ifnet *interface;
};
typedef struct device *device_t;
struct ifnet {
    int if_flags;
    void (*if_stop)(struct ifnet *, int);
    int (*if_init)(struct ifnet *);
};
static bool registration_ok = true, locked;
static unsigned stops, starts, kicks, diagnostics;
static int init_error;
static bool pmf_device_register(device_t dev, void *suspend, void *resume) {
    (void)dev; assert(suspend == NULL && resume == NULL);
    return registration_ok;
}
#define aprint_error_dev(...) (++diagnostics)
#define aprint_normal_ifnet(...) (++diagnostics)
static int splnet(void) { return 0; }
static void splx(int priority) { (void)priority; }
#define IFNET_LOCK(ifp) do { (void)(ifp); assert(!locked); locked = true; } while (0)
#define IFNET_UNLOCK(ifp) do { (void)(ifp); assert(locked); locked = false; } while (0)
static struct ifnet *device_pmf_class_private(device_t dev) { return dev->interface; }
static void device_pmf_class_register(device_t dev, struct ifnet *ifp,
    bool (*suspend)(device_t, const pmf_qual_t *),
    bool (*resume)(device_t, const pmf_qual_t *), void *deregister) {
    assert(deregister == NULL);
    dev->interface = ifp; dev->suspend = suspend; dev->resume = resume;
}
static void if_start_lock(struct ifnet *ifp) { (void)ifp; assert(!locked); kicks++; }
#include "network-pmf.inc"
static void attach_power(device_t self, struct ifnet *ifp) {
    (void)ifp;
#include "driver-registration.inc"
}
static void stop_interface(struct ifnet *ifp, int disable) {
    assert(locked && disable == 0);
    stops++; ifp->if_flags &= ~IFF_RUNNING;
}
static int start_interface(struct ifnet *ifp) {
    assert(locked && !(ifp->if_flags & IFF_RUNNING));
    starts++;
    if (!init_error) ifp->if_flags |= IFF_RUNNING;
    return init_error;
}
int main(void) {
    struct device dev = {0};
    struct ifnet ifp = {.if_flags = IFF_UP | IFF_RUNNING,
        .if_stop = stop_interface, .if_init = start_interface};
    attach_power(&dev, &ifp);
    assert(dev.suspend != NULL && dev.resume != NULL && dev.interface == &ifp);
    assert(dev.suspend(&dev, NULL));
    assert(stops == 1 && !(ifp.if_flags & IFF_RUNNING) && (ifp.if_flags & IFF_UP));
    assert(dev.resume(&dev, NULL));
    assert(starts == 1 && kicks == 1 && (ifp.if_flags & IFF_RUNNING));
    ifp.if_flags = 0;
    assert(dev.suspend(&dev, NULL) && dev.resume(&dev, NULL));
    assert(stops == 2 && starts == 1 && kicks == 1 && ifp.if_flags == 0);
    ifp.if_flags = IFF_UP | IFF_RUNNING; init_error = 5;
    assert(dev.suspend(&dev, NULL) && dev.resume(&dev, NULL));
    assert(starts == 2 && diagnostics == 1 && !(ifp.if_flags & IFF_RUNNING));
    dev = (struct device){0}; registration_ok = false;
    attach_power(&dev, &ifp);
    assert(diagnostics == 2 && dev.suspend == NULL && dev.resume == NULL);
    puts("rtwx PMF registration/lifecycle: PASS");
    return 0;
}
