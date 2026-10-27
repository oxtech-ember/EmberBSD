/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual issuer, config pending/join, ACPI job/finalize and renderer attach. */
static int fixture_config_wait(void);
#include "issuer-fixture.h"
#define aprint_debug_dev print_device_message
#define aprint_error_dev print_device_message

typedef void *bus_dma_tag_t;
typedef void *ACPI_HANDLE;
typedef uint64_t ACPI_INTEGER;
typedef int ACPI_STATUS;
typedef void *cfdata_t;
typedef struct { unsigned Valid; struct { char *String; } HardwareId;
	const char *cid; } ACPI_DEVICE_INFO;
struct acpi_node { int ad_type; ACPI_DEVICE_INFO *ad_devinfo; ACPI_HANDLE ad_handle; };
struct acpi_attach_args { struct acpi_node *aa_node; bus_space_tag_t aa_memt;
	bus_dma_tag_t aa_dmat; };
struct acpi_mem { bus_addr_t ar_base; bus_size_t ar_length; };
struct acpi_resources { int parsed; };
#define ACPI_TYPE_DEVICE 6
#define ACPI_VALID_HID 1
#define ACPI_FAILURE(s) ((s) != 0)
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
static int acpi_resource_parse_ops_default;
static unsigned queue_calls, claim_waits, wakeups, thread_exits;
static unsigned observe_calls, takeover_calls, render_calls;
static int observe_error, takeover_error, register_error;
#ifdef WSDISPLAY_BOOTFB_DEFER
static bool force_immediate;
#endif
static unsigned bad_resource;
static ACPI_INTEGER acpi_sta, acpi_cca;
static int acpi_parse_error;
static struct acpi_mem gpu_resources[3];
static struct device gpu, acpi_parent;

static bool device_pmf_is_registered(device_t d) { CHECK(d == &gpu); return true; }
static void kthread_exit(int n) { CHECK(n == 0); thread_exits++; }
static void cv_broadcast(int *p) { CHECK(p == &config_misc_cv); wakeups++; }
void config_pending_incr(device_t);
void config_pending_decr(device_t);
#include "interrupt-functions.h"
#include <arm/broadcom/bcm2712_v3d_render.h>
#include "acpi-state.h"

#ifdef WSDISPLAY_BOOTFB_DEFER
static void fixture_config_interrupts(device_t d, void (*fn)(device_t))
{
	queue_calls++;
#ifdef WSDISPLAY_BOOTFB_DEFER
	CHECK(((struct bcmv3d_softc *)device_private(d))->sc_bootfb.phase ==
	    BCMV3D_BOOTFB_QUEUED);
#endif
	if (force_immediate)
		cold = 0;
	config_interrupts(d, fn);
}
#endif
static int fixture_finalize_register(device_t d, int (*fn)(device_t))
{
	if (register_error != 0)
		return register_error;
	return config_finalize_register(d, fn);
}
static int acpi_eval_integer(ACPI_HANDLE h, const char *name, ACPI_INTEGER *v)
{
	CHECK(h == (void *)&gpu);
	if (strcmp(name, "_STA") == 0) *v = acpi_sta;
	else { CHECK(strcmp(name, "_CCA") == 0); *v = acpi_cca; }
	return 0;
}
static int acpi_resource_parse(device_t d, ACPI_HANDLE h, const char *name,
    struct acpi_resources *r, void *ops)
{
	CHECK(d == &gpu && h == (void *)&gpu && strcmp(name, "_CRS") == 0);
	CHECK(ops == &acpi_resource_parse_ops_default); r->parsed = 1;
	return acpi_parse_error;
}
static struct acpi_mem *acpi_res_mem(struct acpi_resources *r, unsigned i)
{
	CHECK(r->parsed && i < 3); return bad_resource == i + 1 ? NULL : &gpu_resources[i];
}
static void acpi_resource_cleanup(struct acpi_resources *r) { CHECK(r->parsed); }
static int acpi_match_hid(ACPI_DEVICE_INFO *info, const char *const *ids)
{
	return strcmp(info->cid, ids[0]) == 0;
}
static void bcm2712_display_acpi_observe(device_t d, ACPI_HANDLE h)
{
	CHECK(d == &gpu && h == (void *)&gpu); observe_calls++;
}
static int bcmv3d_observe(struct bcmv3d_softc *s)
{
	CHECK(gpu.dv_pending == 0); CHECK(s == device_private(&gpu)); s->sc_stage = "controlled observer boundary";
	return observe_error;
}
int bcmv3d_takeover_probe(device_t d, bus_space_tag_t tag)
{
	CHECK(d == &gpu && tag == &arm_generic_bs_tag); takeover_calls++;
	return takeover_error;
}
#define config_interrupts fixture_config_interrupts
#define config_finalize_register fixture_finalize_register
#include "acpi-functions.h"
#undef config_interrupts
#undef config_finalize_register

/* DRM/V3D engine services are controlled. The complete renderer body is real. */
typedef int kmutex_t;
typedef int vmem_t;
struct drm_device {
	void *dev_private;
	bus_dma_tag_t bus_dmat, bus_dmat32, dmat;
	struct { unsigned version, length, bus_type; char fullname[32], compatible[32]; } native_platform;
};
struct sysctllog;
#include "bcmv3d_bo_stats.h"
#include "renderer-types.h"
static int bcmv3d_driver;
static unsigned render_failure, renderer_reads, mutexes, va_objects, drm_objects;
static struct bcmv3d_device *published_renderer;
static const struct bcmv3d_bootfb_owner *expected_owner;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define VM_SLEEP 0
#define DRM_NATIVE_PLATFORM_VERSION 1
#define DRM_NATIVE_BUS_PLATFORM 1
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
static bool bcmv3d_takeover_complete(void) { return render_failure != 14; }
int drm_guarantee_initialized(void) { return render_failure == 1 ? EIO : 0; }
static struct drm_device *drm_dev_alloc(int *driver, device_t d)
{
	struct drm_device *p;
	CHECK(driver == &bcmv3d_driver && d == &gpu);
	if (render_failure == 2) return ERR_PTR(-ENOMEM);
	p = calloc(1, sizeof(*p)); CHECK(p != NULL && !IS_ERR(p));
	drm_objects++; return p;
}
static void drm_dev_put(struct drm_device *d)
{
	CHECK(d->dev_private == NULL && drm_objects > 0); drm_objects--; free(d);
}
static void *kmem_zalloc(size_t n, int flag)
{
	void *p = calloc(1, n); CHECK(p != NULL && flag == KM_SLEEP); hook_allocations++; return p;
}
static void mutex_init(kmutex_t *p, int kind, int level)
{
	CHECK(kind == MUTEX_DEFAULT && level == IPL_NONE); *p = 1; mutexes++;
}
static void mutex_destroy(kmutex_t *p) { CHECK(*p == 1 && mutexes > 0); mutexes--; }
static vmem_t *vmem_create(const char *name, uint64_t base, uint64_t size,
    size_t quantum, void *a, void *f, void *src, int limit, int flags, int ipl)
{
	vmem_t *p;
	CHECK(strcmp(name, "v3dva") == 0 && base == BCMV3D_VA_START && size == BCMV3D_VA_SIZE);
	CHECK(quantum == PAGE_SIZE && a == NULL && f == NULL && src == NULL);
	CHECK(limit == 0 && flags == VM_SLEEP && ipl == IPL_NONE);
	if (render_failure == 3) return NULL;
	p = malloc(sizeof(*p)); CHECK(p != NULL); va_objects++; return p;
}
static void vmem_destroy(vmem_t *p) { CHECK(p != NULL && va_objects > 0); va_objects--; free(p); }
static size_t bcmv3d_bo_total(void) { return 64 * 1024 * 1024; }
static int renderer_peek(bool hub, unsigned offset, uint32_t *out)
{
	static const bool expected_hub[7] = { true, true, true, true, false, false, false };
	static const unsigned expected_offset[7] = { 4, 12, 16, 20, 0, 4, 8 };
	unsigned n = renderer_reads++;
	CHECK(n < 7 && hub == expected_hub[n] && offset == expected_offset[n]);
	if (render_failure == 4 + n) return EIO;
	*out = n; return 0;
}
static int bcmv3d_takeover_hub_peek(unsigned o, uint32_t *v) { return renderer_peek(true, o, v); }
static int bcmv3d_takeover_core_peek(unsigned o, uint32_t *v) { return renderer_peek(false, o, v); }
static size_t fixture_strlcpy(char *dst, const char *src, size_t n)
{
	size_t length = strlen(src);
	if (n != 0) { size_t copy = length < n - 1 ? length : n - 1; memcpy(dst, src, copy); dst[copy] = 0; }
	return length;
}
#undef strlcpy
#define strlcpy fixture_strlcpy
static int drm_dev_set_unique(struct drm_device *d, const char *name)
{
	CHECK(d->dev_private != NULL && strcmp(name, "fixture") == 0); return render_failure == 11 ? -EIO : 0;
}
static int bcmv3d_job_init(struct bcmv3d_device *s) { CHECK(s->job == NULL); return render_failure == 12 ? EIO : 0; }
static void bcmv3d_job_fini(struct bcmv3d_device *s) { CHECK(s->job == NULL); }
static void sysctl_teardown(struct sysctllog **p) { CHECK(*p == NULL); }
static int drm_dev_register(struct drm_device *d, int flags)
{
	struct bcmv3d_device *s = d->dev_private;
	CHECK(flags == 0 && s->bootfb == expected_owner);
	if (render_failure == 13) return -EIO;
	published_renderer = s; return 0;
}
static void bcmv3d_bo_sysctl_init(struct bcmv3d_device *s) { CHECK(s == published_renderer); }
#include "renderer-function.h"
#undef strlcpy

static struct bcmv3d_softc gpu_sc;
static ACPI_DEVICE_INFO info;
static struct acpi_node node;
static struct acpi_attach_args aa;
static unsigned pending_waits;
static int fixture_config_wait(void)
{
	CHECK(gpu.dv_pending > 0); pending_waits++;
	config_interrupts_thread(NULL);
	return 0;
}
static void dispose_renderer_services(void)
{
	struct bcmv3d_device *s = published_renderer;
	if (s == NULL) return;
	/* Dispose fake services between independent simulated boots, not a native detach. */
	vmem_destroy(s->va); mutex_destroy(&s->memory_lock);
	s->drm->dev_private = NULL; drm_dev_put(s->drm); kmem_free(s, sizeof(*s));
	published_renderer = NULL;
}
static void claim_fresh(void)
{
	unsigned i;

	dispose_renderer_services();
	CHECK(TAILQ_EMPTY(&interrupt_config_queue) && TAILQ_EMPTY(&config_pending));
	CHECK(drm_objects == 0 && va_objects == 0 && mutexes == 0);
	fresh(); memset(&gpu_sc, 0, sizeof(gpu_sc)); memset(&gpu, 0, sizeof(gpu));
	gpu.private = &gpu_sc; gpu.driver = "bcmv3d"; gpu.dv_parent = &acpi_parent;
	info.Valid = ACPI_VALID_HID; info.HardwareId.String = "BCM2712"; info.cid = "BCM2850";
	node.ad_type = ACPI_TYPE_DEVICE; node.ad_devinfo = &info; node.ad_handle = &gpu;
	aa.aa_node = &node; aa.aa_memt = &arm_generic_bs_tag; aa.aa_dmat = &gpu;
	for (i = 0; i < 3; i++) {
		gpu_resources[i].ar_base = bcmv3d_addresses[i]; gpu_resources[i].ar_length = bcmv3d_lengths[i];
	}
	queue_calls = claim_waits = wakeups = thread_exits = pending_waits = 0;
	observe_calls = takeover_calls = render_calls = 0;
	observe_error = takeover_error = register_error = acpi_parse_error = 0;
#ifdef WSDISPLAY_BOOTFB_DEFER
	force_immediate = false;
#endif
	bad_resource = render_failure = renderer_reads = 0;
	acpi_sta = 15; acpi_cca = 0; expected_owner = NULL;
}
static void attach_gpu(void)
{
	if (bcmv3d_match(NULL, NULL, &aa) != 0) bcmv3d_attach(NULL, &gpu, &aa);
}
static void selected(void) { fixture_address = 0; fixture_handle = 0; arm_simplefb_preattach(); attach_selected(1); }
static void run_finish(void)
{
	cold = 0;
	CHECK(pdevinit[0].pdev_attach == NULL);
	genfb_mapping = true; config_finalize(); genfb_mapping = false;
	CHECK(hook_allocations == hook_frees + (published_renderer != NULL));
	CHECK(kernel_depth == 1 && gpu.dv_pending == 0 && TAILQ_EMPTY(&interrupt_config_queue));
}
static void success_cases(void)
{
	unsigned first, calls;

	for (first = 0; first < 2; first++) {
		claim_fresh();
		if (first) { attach_gpu(); selected(); } else { selected(); attach_gpu(); }
#ifdef WSDISPLAY_BOOTFB_DEFER
		CHECK(queue_calls == 1 && gpu.dv_pending == 1);
		expected_owner = &gpu_sc.sc_bootfb;
#else
		CHECK(queue_calls == 0 && gpu.dv_pending == 0);
#endif
		run_finish();
		CHECK(published_renderer != NULL && published_renderer->bootfb == expected_owner);
#ifdef WSDISPLAY_BOOTFB_DEFER
		CHECK(genfb_attaches == 1);
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CANCELED);
		CHECK(gpu_sc.sc_bootfb.lease == NULL);
		CHECK(gpu_sc.sc_bootfb.framebuffer.sf_address == 0 && gpu_sc.sc_bootfb.framebuffer.sf_width == fixture_width);
		CHECK(wsdisplay_bootfb.phase == BOOTFB_FALLBACK_DONE && pending_waits == 1);
		bcmv3d_bootfb_claim_job(&gpu); bcmv3d_bootfb_queue(&gpu_sc); CHECK(queue_calls == 1);
#endif
		calls = observe_calls; CHECK(bcmv3d_finalize(&gpu) == 0 && observe_calls == calls);
	}
}
static void failure_cases(void)
{
#ifdef WSDISPLAY_BOOTFB_DEFER
	unsigned n;

	for (n = 1; n <= 14; n++) {
		claim_fresh(); selected(); attach_gpu(); render_failure = n;
		expected_owner = &gpu_sc.sc_bootfb; run_finish();
		CHECK(published_renderer == NULL && gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CANCELED);
		CHECK(gpu_sc.sc_bootfb.lease == NULL && gpu_sc.sc_bootfb.first_error != 0 && gpu_sc.sc_bootfb.cancel_error == 0);
		CHECK(genfb_attaches == 1 && drm_objects == 0 && va_objects == 0 && mutexes == 0);
	}
	claim_fresh(); selected(); attach_gpu(); observe_error = EIO; run_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CANCELED && genfb_attaches == 1);
	claim_fresh(); selected(); attach_gpu(); takeover_error = EIO; run_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_RETAINED && wsdisplay_bootfb.phase == BOOTFB_RETAINED);
	CHECK(gpu_sc.sc_bootfb.lease != NULL && genfb_attaches == 0 && published_renderer == NULL);
	claim_fresh(); selected(); attach_gpu(); cold = 0; config_interrupts_thread(NULL);
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLAIMED);
	wsdisplay_console_conf.emulcookie = &display; observe_error = EIO; run_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_RETAINED && gpu_sc.sc_bootfb.first_error == EIO);
	CHECK(gpu_sc.sc_bootfb.cancel_error != 0 && genfb_attaches == 0);
#endif
}
static void gate_cases(void)
{
	unsigned n;

	for (n = 0; n < 11; n++) {
		claim_fresh(); selected();
		switch (n) {
		case 0: node.ad_type = 0; break;
		case 1: info.HardwareId.String = "other"; break;
		case 2: info.cid = "other"; break;
		case 3: acpi_sta = 0; break;
		case 4: acpi_cca = 1; break;
		case 5: acpi_parse_error = EIO; break;
		case 6: bad_resource = 2; break;
		case 7: register_error = EIO; break;
		case 8: cold = 0; break;
		case 9: aa.aa_memt = NULL; break;
		case 10: aa.aa_dmat = NULL; break;
		}
		attach_gpu(); CHECK(queue_calls == 0);
		observe_error = EIO; run_finish();
		CHECK(genfb_attaches == 1 && published_renderer == NULL);
	}
#ifdef WSDISPLAY_BOOTFB_DEFER
	claim_fresh(); attach_gpu(); run_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_REFUSED && published_renderer != NULL);
	CHECK(published_renderer->bootfb == NULL && gpu_sc.sc_bootfb.lease == NULL);
	claim_fresh(); selected(); force_immediate = true; attach_gpu();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLAIMED && gpu.dv_pending == 0);
	CHECK(observe_calls == 0 && takeover_calls == 0 && renderer_reads == 0);
	expected_owner = &gpu_sc.sc_bootfb; run_finish();
	claim_fresh(); selected(); attach_gpu(); cold = 0;
	config_process_deferred(&interrupt_config_queue, &acpi_parent);
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLAIMED && gpu.dv_pending == 0);
	expected_owner = &gpu_sc.sc_bootfb; run_finish();
#endif
}
static void renderer_output_cases(void)
{
	struct bcmv3d_device canary, *out;
	struct bcmv3d_device *sc;
	unsigned n;
	int tuple;

	claim_fresh();
	CHECK(bcmv3d_render_attach(&gpu, &arm_generic_bs_tag, &gpu, &gpu,
	    NULL, NULL) == EINVAL);
	CHECK(hook_allocations == hook_frees && drm_objects == 0);
	for (n = 1; n <= 14; n++) {
		claim_fresh(); render_failure = n; out = &canary;
		CHECK(bcmv3d_render_attach(&gpu, &arm_generic_bs_tag, &gpu, &gpu,
		    NULL, &out) != 0);
		CHECK(out == &canary && published_renderer == NULL);
	}
	claim_fresh(); out = &canary;
#ifdef WSDISPLAY_BOOTFB_DEFER
	expected_owner = &gpu_sc.sc_bootfb;
#else
	expected_owner = (const struct bcmv3d_bootfb_owner *)&gpu_sc;
#endif
	CHECK(bcmv3d_render_attach(&gpu, &arm_generic_bs_tag, &gpu, &gpu,
	    expected_owner, &out) == 0);
	CHECK(out == published_renderer && out != &canary);
	sc = out;
	CHECK(bcmv3d_render_bootfb_validate(sc, &gpu, &arm_generic_bs_tag,
	    &gpu, &gpu, expected_owner) == 0);
	for (n = 0; n < 5; n++) {
		struct bcmv3d_device saved = *sc;
		if (n == 0) sc->dev = (device_t)&canary;
		if (n == 1) sc->bst = (bus_space_tag_t)&canary;
		if (n == 2) sc->dmat = &canary;
		if (n == 3) sc->handle = &canary;
		if (n == 4) sc->bootfb = NULL;
		tuple = bcmv3d_render_bootfb_validate(sc, &gpu, &arm_generic_bs_tag,
		    &gpu, &gpu, expected_owner);
		CHECK(tuple == ENXIO);
		*sc = saved;
	}
	CHECK(bcmv3d_render_bootfb_validate(NULL, &gpu, &arm_generic_bs_tag,
	    &gpu, &gpu, expected_owner) == EINVAL);
	dispose_renderer_services();
}
int main(void)
{
	config_process_deferred(&interrupt_config_queue, NULL);
	success_cases(); failure_cases(); gate_cases(); renderer_output_cases(); dispose_renderer_services();
	CHECK(hook_allocations == hook_frees && kernel_depth == 1);
	printf("boot framebuffer claim job: %u checks PASS\n", checks);
	return 0;
}
