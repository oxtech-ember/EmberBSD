# Native CMA import ownership

The native CMA helper accepts one contiguous DMA segment. Imported pages
belong to the exporter. The importer must not pass these pages to
`bus_dmamem_free` or `vmem_xfree`.

`bus_dmamem_import_pages` combines each adjacent page with the full current
segment. It returns `EFBIG` before it exceeds the supplied segment array.
It rejects an empty page list or a nonpositive segment capacity with `EINVAL`.
On error, it does not publish `rsegs`. It can have written partial segments;
the caller must discard these segment descriptors.

The CMA constructor stores the imported SG table after local map creation
succeeds. If creation fails, it frees only local metadata and mappings.
The generic PRIME caller then unmaps and detaches the failed import.
For a successful import, final object release calls `drm_prime_gem_destroy`
after removing the local DMA map and CPU mapping. This releases the mapped
attachment and its exporter reference. Owned CMA allocations keep their
existing allocator cleanup.

The generic contract is in
[`drm_gem_prime_import_dev` and `drm_prime_gem_destroy`](../../dist/drm/drm_prime.c).
This change preserves the upstream license and identifiers. The changes and
regression are AI-assisted EmberBSD work.

## Run the contract

From the source root, run:

```sh
python3 ember/tools/drm-cma-import-contract.py
CMA_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DNDEBUG' \
    python3 ember/tools/drm-cma-import-contract.py
```

The runner requires Python standard library modules and a native C compiler.
`CC` selects that compiler. `TMPDIR` selects the temporary parent directory.
`TEST_RUNNER` can select Valgrind on a supported host. For example:

```sh
TEST_RUNNER='valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=99' \
    python3 ember/tools/drm-cma-import-contract.py
```

The runner extracts the actual page-import and CMA allocation/release bodies.
Kernel doubles check segment capacity, address wrap, borrowed-page lifetime,
all local map failure paths, owned allocation failures, and pool cleanup.
`CMA_SOURCE_ROOT` selects another source root for causal comparisons.
`CMA_CASE=cma-baseline` isolates the successful import lifetime check.
A compiler error is not an accepted causal failure.

These checks do not qualify a DMA tag, IOMMU configuration, device import,
scanout, or cross-device fence synchronization. Those checks require the
consumer driver and its actual hardware path.
