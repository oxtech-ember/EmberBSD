/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; native bus_dma ownership, no vendor code. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_npu_mmu.h>
#include <arm/sunxi/sun60i_a733_npu_dma.h>
#include <arm/sunxi/sun60i_a733_npu_run.h>

#define NPU_DMA_PAGE 4096
#define NPU_DMA_LIMIT (UINT64_C(1) << 32)
#define NPU_DMA_COMMAND_VA UINT32_C(0x01001000)
#define NPU_DMA_NBUFFERS 3
#define NPU_DMA_TASK_NBUFFERS 6

struct npu_dma_buffer {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *cpu;
	size_t bytes;
	bus_addr_t address;
	int nsegments;
	bool loaded;
};

struct sun60i_a733_npu_dma {
	bus_dma_tag_t tag;
	struct npu_dma_buffer buffers[NPU_DMA_TASK_NBUFFERS];
	size_t command_bytes, constants_bytes, input_bytes;
	uint32_t pd_entry, profile;
	bool retained;
	struct sun60i_a733_npu_run *task_run;
	const struct sun60i_a733_npu_dma_task_reference *task_reference;
	struct sun60i_a733_npu_dma_task_report task_report;
};

static size_t
npu_dma_buffer_count(const struct sun60i_a733_npu_dma *m)
{

	return m->profile == A733_NPU_DMA_PROFILE_FE ? NPU_DMA_NBUFFERS :
	    m->profile == A733_NPU_DMA_PROFILE_TASK ? NPU_DMA_TASK_NBUFFERS : 0;
}

static void
npu_dma_buffer_free(bus_dma_tag_t tag, struct npu_dma_buffer *b)
{
	if (b->loaded)
		bus_dmamap_unload(tag, b->map);
	if (b->map != NULL)
		bus_dmamap_destroy(tag, b->map);
	if (b->cpu != NULL)
		bus_dmamem_unmap(tag, b->cpu, b->bytes);
	if (b->nsegments != 0)
		bus_dmamem_free(tag, &b->segment, b->nsegments);
	memset(b, 0, sizeof(*b));
}

static int
npu_dma_buffer_alloc(bus_dma_tag_t tag, size_t bytes, struct npu_dma_buffer *b)
{
	int error;

	b->bytes = bytes;
	error = bus_dmamem_alloc(tag, bytes, NPU_DMA_PAGE, 0, &b->segment,
	    1, &b->nsegments, BUS_DMA_WAITOK);
	if (error != 0) {
		b->nsegments = 0;
		return error;
	}
	if (b->nsegments != 1 || b->segment.ds_len != bytes) {
		error = EINVAL;
		goto fail;
	}
	error = bus_dmamem_map(tag, &b->segment, 1, bytes, &b->cpu,
	    BUS_DMA_WAITOK);
	if (error != 0) {
		b->cpu = NULL;
		goto fail;
	}
	error = bus_dmamap_create(tag, bytes, 1, bytes, 0, BUS_DMA_WAITOK,
	    &b->map);
	if (error != 0) {
		b->map = NULL;
		goto fail;
	}
	error = bus_dmamap_load(tag, b->map, b->cpu, bytes, NULL, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	b->loaded = true;
	/* A bounce map must not replace the context's CPU/device page identity. */
	if ((b->map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0) {
		error = EOPNOTSUPP;
		goto fail;
	}
	if (b->map->dm_nsegs != 1 || b->map->dm_mapsize != bytes ||
	    b->map->dm_segs[0].ds_len != bytes) {
		error = EINVAL;
		goto fail;
	}
	b->address = b->map->dm_segs[0].ds_addr;
	if ((b->address & (NPU_DMA_PAGE - 1)) != 0 ||
	    b->address >= NPU_DMA_LIMIT || bytes > NPU_DMA_LIMIT - b->address) {
		error = ERANGE;
		goto fail;
	}
	memset(b->cpu, 0, bytes);
	return 0;
fail:
	npu_dma_buffer_free(tag, b);
	return error;
}

int
sun60i_a733_npu_dma_destroy(struct sun60i_a733_npu_dma *m)
{
	size_t count;

	if (m == NULL)
		return EINVAL;
	count = npu_dma_buffer_count(m);
	if (count == 0)
		return EINVAL;
	if (m->retained)
		return EBUSY;
	for (size_t i = count; i > 0; i--)
		npu_dma_buffer_free(m->tag, &m->buffers[i - 1]);
	bus_dmatag_destroy(m->tag);
	kmem_free(m, sizeof(*m));
	return 0;
}

int
sun60i_a733_npu_dma_prepare(bus_dma_tag_t parent, const void *commands,
    size_t bytes, struct sun60i_a733_npu_dma **out)
{
	static const size_t sizes[NPU_DMA_NBUFFERS] = {
		NPU_DMA_PAGE, A733_NPU_MMU_STLB_BYTES, NPU_DMA_PAGE
	};
	struct sun60i_a733_npu_dma *m;
	struct sun60i_a733_npu_mmu_page page;
	struct sun60i_a733_npu_mmu_plan plan;
	int error;

	if (parent == NULL || commands == NULL || out == NULL ||
	    bytes < 8 || bytes > NPU_DMA_PAGE || (bytes & 7) != 0 ||
	    bytes > UINTPTR_MAX - (uintptr_t)commands)
		return EINVAL;
	m = kmem_zalloc(sizeof(*m), KM_SLEEP);
	m->profile = A733_NPU_DMA_PROFILE_FE;
	error = bus_dmatag_subregion(parent, 0, NPU_DMA_LIMIT - 1,
	    &m->tag, BUS_DMA_WAITOK);
	if (error != 0) {
		kmem_free(m, sizeof(*m));
		return error;
	}
	for (size_t i = 0; i < NPU_DMA_NBUFFERS; i++) {
		error = npu_dma_buffer_alloc(m->tag, sizes[i], &m->buffers[i]);
		if (error != 0)
			goto fail;
		for (size_t j = 0; j < i; j++) {
			if (m->buffers[i].address < m->buffers[j].address + sizes[j] &&
			    m->buffers[j].address < m->buffers[i].address + sizes[i]) {
				error = EEXIST;
				goto fail;
			}
		}
	}
	page = (struct sun60i_a733_npu_mmu_page){ m->buffers[2].address, 0 };
	plan = (struct sun60i_a733_npu_mmu_plan){
		.mtlb_physical = m->buffers[0].address,
		.stlb_physical = m->buffers[1].address,
		.virtual_base = NPU_DMA_COMMAND_VA, .pages = &page, .page_count = 1
	};
	error = sun60i_a733_npu_mmu_build(&plan, m->buffers[0].cpu, sizes[0],
	    m->buffers[1].cpu, sizes[1]);
	if (error != 0)
		goto fail;
	error = sun60i_a733_npu_mmu_pd_entry(m->buffers[0].address, &m->pd_entry);
	if (error != 0)
		goto fail;
	memcpy(m->buffers[2].cpu, commands, bytes);
	m->command_bytes = bytes;
	*out = m;
	return 0;
fail:
	(void)sun60i_a733_npu_dma_destroy(m);
	return error;
}

int
sun60i_a733_npu_dma_info(const struct sun60i_a733_npu_dma *m,
    struct sun60i_a733_npu_dma_info *out)
{
	if (m == NULL || out == NULL)
		return EINVAL;
	*out = (struct sun60i_a733_npu_dma_info){
		.mtlb_address = m->buffers[0].address,
		.stlb_address = m->buffers[1].address,
		.command_address = m->buffers[2].address, .pd_entry = m->pd_entry,
		.command_virtual = NPU_DMA_COMMAND_VA,
		.command_bytes = m->command_bytes, .retained = m->retained,
		.profile = m->profile
	};
	return 0;
}

static bool
npu_dma_spans_overlap(const void *p, size_t bytes, const void *q, size_t size)
{
	uintptr_t a = (uintptr_t)p, b = (uintptr_t)q;

	if (bytes > UINTPTR_MAX - a || size > UINTPTR_MAX - b)
		return true;
	return a < b + size && b < a + bytes;
}

int
sun60i_a733_npu_dma_task_prepare(bus_dma_tag_t parent, const void *commands,
    size_t command_bytes, const void *constants, size_t constants_bytes,
    const void *input, size_t input_bytes, struct sun60i_a733_npu_dma **out)
{
	static const size_t sizes[NPU_DMA_TASK_NBUFFERS] = {
		NPU_DMA_PAGE, A733_NPU_MMU_STLB_BYTES, NPU_DMA_PAGE,
		NPU_DMA_PAGE, NPU_DMA_PAGE, NPU_DMA_PAGE
	};
	struct sun60i_a733_npu_mmu_page pages[4];
	struct sun60i_a733_npu_mmu_plan plan;
	struct sun60i_a733_npu_dma *m;
	int error;

	if (parent == NULL || commands == NULL || constants == NULL ||
	    input == NULL || out == NULL || command_bytes < 8 ||
	    command_bytes > NPU_DMA_PAGE || (command_bytes & 7) != 0 ||
	    constants_bytes == 0 || constants_bytes > NPU_DMA_PAGE ||
	    input_bytes == 0 || input_bytes > NPU_DMA_PAGE ||
	    command_bytes > UINTPTR_MAX - (uintptr_t)commands ||
	    constants_bytes > UINTPTR_MAX - (uintptr_t)constants ||
	    input_bytes > UINTPTR_MAX - (uintptr_t)input ||
	    sizeof(*out) > UINTPTR_MAX - (uintptr_t)out ||
	    npu_dma_spans_overlap(out, sizeof(*out), commands, command_bytes) ||
	    npu_dma_spans_overlap(out, sizeof(*out), constants, constants_bytes) ||
	    npu_dma_spans_overlap(out, sizeof(*out), input, input_bytes))
		return EINVAL;
	m = kmem_zalloc(sizeof(*m), KM_SLEEP);
	m->profile = A733_NPU_DMA_PROFILE_TASK;
	error = bus_dmatag_subregion(parent, 0, NPU_DMA_LIMIT - 1,
	    &m->tag, BUS_DMA_WAITOK);
	if (error != 0) {
		kmem_free(m, sizeof(*m));
		return error;
	}
	for (size_t i = 0; i < NPU_DMA_TASK_NBUFFERS; i++) {
		error = npu_dma_buffer_alloc(m->tag, sizes[i], &m->buffers[i]);
		if (error != 0)
			goto fail;
		for (size_t j = 0; j < i; j++) {
			if (m->buffers[i].address < m->buffers[j].address + sizes[j] &&
			    m->buffers[j].address < m->buffers[i].address + sizes[i]) {
				error = EEXIST;
				goto fail;
			}
		}
	}
	if (m->buffers[2].address == NPU_DMA_COMMAND_VA) {
		error = EADDRINUSE;
		goto fail;
	}
	for (size_t i = 0; i < 4; i++) {
		pages[i] = (struct sun60i_a733_npu_mmu_page){
			m->buffers[i + 2].address,
			i == 3 ? A733_NPU_MMU_WRITABLE : 0
		};
	}
	plan = (struct sun60i_a733_npu_mmu_plan){
		.mtlb_physical = m->buffers[0].address,
		.stlb_physical = m->buffers[1].address,
		.virtual_base = NPU_DMA_COMMAND_VA, .pages = pages, .page_count = 4
	};
	error = sun60i_a733_npu_mmu_build(&plan, m->buffers[0].cpu, sizes[0],
	    m->buffers[1].cpu, sizes[1]);
	if (error != 0)
		goto fail;
	error = sun60i_a733_npu_mmu_pd_entry(m->buffers[0].address, &m->pd_entry);
	if (error != 0)
		goto fail;
	memcpy(m->buffers[2].cpu, commands, command_bytes);
	memcpy(m->buffers[3].cpu, constants, constants_bytes);
	memcpy(m->buffers[4].cpu, input, input_bytes);
	memset(m->buffers[5].cpu, A733_NPU_DMA_TASK_OUTPUT_POISON, NPU_DMA_PAGE);
	m->command_bytes = command_bytes;
	m->constants_bytes = constants_bytes;
	m->input_bytes = input_bytes;
	*out = m;
	return 0;
fail:
	(void)sun60i_a733_npu_dma_destroy(m);
	return error;
}

int
sun60i_a733_npu_dma_task_info(const struct sun60i_a733_npu_dma *m,
    struct sun60i_a733_npu_dma_task_info *out)
{
	struct sun60i_a733_npu_dma_task_info info = { 0 };

	if (m == NULL || out == NULL || m->profile != A733_NPU_DMA_PROFILE_TASK ||
	    npu_dma_spans_overlap(out, sizeof(*out), m, sizeof(*m)))
		return EINVAL;
	for (size_t i = 0; i < NPU_DMA_TASK_NBUFFERS; i++) {
		if (npu_dma_spans_overlap(out, sizeof(*out), m->buffers[i].cpu,
		    m->buffers[i].bytes))
			return EINVAL;
	}
	(void)sun60i_a733_npu_dma_info(m, &info.memory);
	info.constants_address = m->buffers[3].address;
	info.input_address = m->buffers[4].address;
	info.output_address = m->buffers[5].address;
	info.constants_virtual = NPU_DMA_COMMAND_VA + NPU_DMA_PAGE;
	info.input_virtual = NPU_DMA_COMMAND_VA + 2 * NPU_DMA_PAGE;
	info.output_virtual = NPU_DMA_COMMAND_VA + 3 * NPU_DMA_PAGE;
	info.constants_bytes = m->constants_bytes;
	info.input_bytes = m->input_bytes;
	info.output_bytes = NPU_DMA_PAGE;
	*out = info;
	return 0;
}

int
sun60i_a733_npu_dma_set_commands(struct sun60i_a733_npu_dma *m,
    const void *commands, size_t bytes)
{
	static const size_t sizes[NPU_DMA_NBUFFERS] = {
		NPU_DMA_PAGE, A733_NPU_MMU_STLB_BYTES, NPU_DMA_PAGE
	};

	if (m == NULL || m->profile != A733_NPU_DMA_PROFILE_FE ||
	    commands == NULL || bytes < 8 ||
	    bytes > NPU_DMA_PAGE || (bytes & 7) != 0 ||
	    bytes > UINTPTR_MAX - (uintptr_t)commands)
		return EINVAL;
	if (m->retained)
		return EBUSY;
	if (npu_dma_spans_overlap(commands, bytes, m, sizeof(*m)))
		return EINVAL;
	for (size_t i = 0; i < NPU_DMA_NBUFFERS; i++) {
		const struct npu_dma_buffer *b = &m->buffers[i];

		if (!b->loaded || b->map == NULL || b->cpu == NULL ||
		    b->bytes != sizes[i] ||
		    npu_dma_spans_overlap(commands, bytes, b->cpu, b->bytes))
			return EINVAL;
	}
	/* Every validation precedes the first output change. No DMA sync occurs. */
	memset(m->buffers[2].cpu, 0, NPU_DMA_PAGE);
	memcpy(m->buffers[2].cpu, commands, bytes);
	m->command_bytes = bytes;
	return 0;
}

int
sun60i_a733_npu_dma_retain(struct sun60i_a733_npu_dma *m)
{
	size_t count;

	if (m == NULL)
		return EINVAL;
	count = npu_dma_buffer_count(m);
	if (count == 0)
		return EINVAL;
	if (m->retained)
		return EBUSY;
	for (size_t i = 0; i < count; i++) {
		if (!m->buffers[i].loaded)
			return EINVAL;
	}
	/* Retain before the first sync and every later hardware write attempt. */
	m->retained = true;
	for (size_t i = 0; i < count; i++)
		bus_dmamap_sync(m->tag, m->buffers[i].map, 0, m->buffers[i].bytes,
		    BUS_DMASYNC_PREWRITE |
		    (m->profile == A733_NPU_DMA_PROFILE_TASK && i == 5 ?
		    BUS_DMASYNC_PREREAD : 0));
	return 0;
}

/* No DMA data is read by the metadata and external-span checks. */
static int
npu_dma_task_metadata(const struct sun60i_a733_npu_dma *m)
{
	static const size_t sizes[NPU_DMA_TASK_NBUFFERS] = {
		NPU_DMA_PAGE, A733_NPU_MMU_STLB_BYTES, NPU_DMA_PAGE,
		NPU_DMA_PAGE, NPU_DMA_PAGE, NPU_DMA_PAGE
	};
	uint32_t pd_entry;

	if (m == NULL || sizeof(*m) > UINTPTR_MAX - (uintptr_t)m ||
	    m->profile != A733_NPU_DMA_PROFILE_TASK || m->tag == NULL ||
	    m->command_bytes != A733_NPU_NN_COMMAND_BYTES ||
	    m->constants_bytes != A733_NPU_NN_CONSTANT_BYTES ||
	    m->input_bytes != A733_NPU_NN_INPUT_BYTES)
		return EINVAL;
	for (size_t i = 0; i < NPU_DMA_TASK_NBUFFERS; i++) {
		const struct npu_dma_buffer *b = &m->buffers[i];

		if (!b->loaded || b->cpu == NULL || b->map == NULL ||
		    b->bytes != sizes[i] || b->nsegments != 1 ||
		    b->segment.ds_len != sizes[i] ||
		    b->segment.ds_addr != b->address ||
		    sizeof(*b->map) > UINTPTR_MAX - (uintptr_t)b->map ||
		    sizes[i] > UINTPTR_MAX - (uintptr_t)b->cpu ||
		    (b->address & (NPU_DMA_PAGE - 1)) != 0 ||
		    b->address >= NPU_DMA_LIMIT || sizes[i] > NPU_DMA_LIMIT - b->address ||
		    (b->map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0 ||
		    b->map->dm_nsegs != 1 || b->map->dm_mapsize != sizes[i] ||
		    b->map->dm_segs[0].ds_len != sizes[i] ||
		    b->map->dm_segs[0].ds_addr != b->address ||
		    npu_dma_spans_overlap(b->cpu, sizes[i], m, sizeof(*m)))
			return EINVAL;
		for (size_t j = 0; j < i; j++) {
			if ((b->address < m->buffers[j].address + sizes[j] &&
			    m->buffers[j].address < b->address + sizes[i]) ||
			    b->map == m->buffers[j].map ||
			    npu_dma_spans_overlap(b->cpu, sizes[i],
			    m->buffers[j].cpu, sizes[j]))
				return EINVAL;
		}
	}
	if (m->buffers[2].address == NPU_DMA_COMMAND_VA ||
	    sun60i_a733_npu_mmu_pd_entry(m->buffers[0].address, &pd_entry) != 0 ||
	    m->pd_entry != pd_entry)
		return EINVAL;
	return 0;
}

static bool
npu_dma_task_external(const struct sun60i_a733_npu_dma *m,
    const void *p, size_t bytes)
{

	if (p == NULL || bytes > UINTPTR_MAX - (uintptr_t)p ||
	    npu_dma_spans_overlap(p, bytes, m, sizeof(*m)))
		return false;
	for (size_t i = 0; i < NPU_DMA_TASK_NBUFFERS; i++) {
		const struct npu_dma_buffer *b = &m->buffers[i];

		if (npu_dma_spans_overlap(p, bytes, b->cpu, b->bytes) ||
		    npu_dma_spans_overlap(p, bytes, b->map, sizeof(*b->map)))
			return false;
	}
	return true;
}

static struct sun60i_a733_npu_dma_task_report
npu_dma_task_compare(const struct sun60i_a733_npu_dma *m,
    const struct sun60i_a733_npu_dma_task_reference *r, bool output)
{
	const uint8_t *refs[NPU_DMA_TASK_NBUFFERS] = {
		r->mtlb, r->stlb, r->commands, r->image.constants,
		r->image.input, r->image.expected
	};
	const size_t lengths[NPU_DMA_TASK_NBUFFERS] = {
		sizeof(r->mtlb), sizeof(r->stlb), sizeof(r->commands),
		sizeof(r->image.constants), sizeof(r->image.input),
		output ? sizeof(r->image.expected) : 0
	};
	struct sun60i_a733_npu_dma_task_report report = { .valid = 1, .pass = 1 };

	for (size_t i = 0; i < NPU_DMA_TASK_NBUFFERS; i++) {
		const uint8_t *data = m->buffers[i].cpu;

		report.first[i] = UINT32_MAX;
		for (size_t j = 0; j < m->buffers[i].bytes; j++) {
			uint8_t expected = j < lengths[i] ? refs[i][j] :
			    i == 5 ? A733_NPU_DMA_TASK_OUTPUT_POISON : 0;

			if (data[j] == expected)
				continue;
			if (report.mismatches[i]++ == 0)
				report.first[i] = j;
			report.pass = 0;
		}
	}
	return report;
}

int
sun60i_a733_npu_dma_task_validate(const struct sun60i_a733_npu_dma *m,
    const struct sun60i_a733_npu_dma_task_reference *r)
{

	if (npu_dma_task_metadata(m) != 0 ||
	    !npu_dma_task_external(m, r, sizeof(*r)))
		return EINVAL;
	if (m->retained)
		return EBUSY;
	if (!npu_dma_task_compare(m, r, false).pass)
		return EIO;
	return 0;
}

int
sun60i_a733_npu_dma_task_retain_checked(struct sun60i_a733_npu_dma *m,
    struct sun60i_a733_npu_run *run,
    const struct sun60i_a733_npu_dma_task_reference *r)
{
	int error;

	if (npu_dma_task_metadata(m) != 0 ||
	    !npu_dma_task_external(m, run, sizeof(*run)) ||
	    !npu_dma_task_external(m, r, sizeof(*r)) ||
	    npu_dma_spans_overlap(run, sizeof(*run), r, sizeof(*r)))
		return EINVAL;
	error = sun60i_a733_npu_dma_task_validate(m, r);
	if (error != 0)
		return error;
	m->task_run = run;
	m->task_reference = r;
	/* The reference, context and all six maps survive the first sync. */
	m->retained = true;
	for (size_t i = 0; i < NPU_DMA_TASK_NBUFFERS; i++)
		bus_dmamap_sync(m->tag, m->buffers[i].map, 0, m->buffers[i].bytes,
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	return 0;
}

int
sun60i_a733_npu_dma_task_check(struct sun60i_a733_npu_dma *m,
    struct sun60i_a733_npu_run *run,
    const struct sun60i_a733_npu_dma_task_reference *r,
    struct sun60i_a733_npu_dma_task_report *out)
{
	struct sun60i_a733_npu_dma_task_report report;

	if (npu_dma_task_metadata(m) != 0 ||
	    !npu_dma_task_external(m, run, sizeof(*run)) ||
	    !npu_dma_task_external(m, r, sizeof(*r)) ||
	    !npu_dma_task_external(m, out, sizeof(*out)) ||
	    npu_dma_spans_overlap(run, sizeof(*run), r, sizeof(*r)) ||
	    npu_dma_spans_overlap(out, sizeof(*out), run, sizeof(*run)) ||
	    npu_dma_spans_overlap(out, sizeof(*out), r, sizeof(*r)))
		return EINVAL;
	if (!m->retained)
		return EBUSY;
	if (m->task_run != run || m->task_reference != r)
		return EINVAL;
	if (m->task_report.valid)
		return EALREADY;
	if (!sun60i_a733_npu_run_task_completed(run, m))
		return EBUSY;
	for (size_t i = 0; i < NPU_DMA_TASK_NBUFFERS; i++)
		bus_dmamap_sync(m->tag, m->buffers[i].map, 0, m->buffers[i].bytes,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	report = npu_dma_task_compare(m, r, true);
	m->task_report = report;
	*out = report;
	return report.pass ? 0 : EIO;
}

bool
sun60i_a733_npu_dma_task_verified(const struct sun60i_a733_npu_dma *m,
    const struct sun60i_a733_npu_run *run)
{

	return m != NULL && run != NULL &&
	    m->profile == A733_NPU_DMA_PROFILE_TASK && m->retained &&
	    m->task_run == run && m->task_reference != NULL &&
	    m->task_report.valid == 1 && m->task_report.pass == 1;
}
