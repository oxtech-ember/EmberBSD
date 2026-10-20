# Native CM5 OpenGL integration

Goal: run hardware OpenGL through the common Mesa package on CM5, then use
that same provider for KWin/Plasma. The user's selected desktop target is CM5.
This continues the accepted [binary synchronization](bcm2712-v3d-sync.md).

## Design and implementation plan

Execute inline. Preserve the existing serialized job lock, per-submission
GEM references, MMU table, cache protocol and poison retention. Add standard
SUBMIT_CL for optional binning followed by rendering. Both input fences must
be snapshotted before the output replacement. Validate command-list ranges,
tile allocation/state addresses, copied handles and unsupported extensions.
Use the accepted native IRQ ownership, with the expected completion selected
for each phase. An OOM or uncertain failure stops the engine and retains BOs;
automatic overflow allocation and arbitrary-job recovery are not promised.

The first hardware gate is a userspace clear/store command list with guarded
buffers. The next is real Mesa shaders and pixel readback, with explicit V3D
identity and no software fallback. Rendering and display/KMS are separate:
The first presentation target is now HDMI on CM5, followed by remote Plasma.
Both require an accepted output path and the common Qt/KWin package migration.

- [x] Add actual-source CL contracts for validation, both dependencies,
  completion IRQ identity/count, cache order, memory ownership and failure.
  Confirm rejection before adding the implementation. Preserve TFU/CSD tests.
- [x] Implement CL admission/execution in the native driver and ioctl table.
  Check optional binning, repeated jobs, same-handle fences, close races and
  every failing register operation. Run static and memory analysis.
- [x] Cross-build a matched kernel and userspace clear/store acceptance tool.
  Review ownership and MMIO sequencing before physical execution. Preserve
  and verify the ordinary kernel/module bundle for recovery.
- [x] Add V3D to the canonical current Mesa recipe through EmberBSD-Ports.
  Repair actual portability failures and keep existing VirGL/LLVM consumers.
  Build and test the package using recorded EmberBSD sysroot provenance.
- [x] Test real EGL/GL V3D rendering on CM5. Fix the first causal failure,
  retaining logs and regressions. Rendering success must include pixel checks.
- [ ] Connect presentation, then current KWin/Plasma; check renderer identity,
  input, repaint and repeated session lifecycle. Keep unaccepted stages explicit.

## Global constraints and review focus

Current Mesa is 26.2.4 (upstream release index checked 2026-10-11); reuse the
common LLVM23/GCC16 dependencies. Cross-build on the development host and
execute on CM5. Do not install a second permanent graphics stack. Keep source
and reusable checks in their owning public repository, with no private paths.

Review command ranges crossing BO boundaries, BO handle closure during a
wait, stale or duplicate IRQs, changed page tables with live GPU references,
and CPU ownership returned before cache clean. Include each in the CL tests.
The native driver must not advertise unimplemented feature bits. A compiled
Mesa driver does not prove GL support or an accelerated desktop.

The CL host model passes 51,611 ownership/IRQ checks, 64 injected register
failures and nine rejected causal mutants. ASan/UBSan passes. CSD, TFU,
binary syncobj, BO and UAPI regressions pass. Fresh review exposed a dependency
snapshot race: closing an aliased syncobj in the first input wait prevented
capture of the second input. The regression failed before the repair. Both
input fences and the output reference are now retained before any wait.
The subsequent hardware CL result is recorded below; Mesa acceptance remains
a separate gate.

## Reproduce the native CL gate

Run `sh ember/tools/bcm2712-v3d-cl-contract.sh` on the build host. Cross-build
`EMBERV3DRENDER`, then build the static checker from the same source checkout:

```sh
CC=/absolute/cross/bin/aarch64--netbsd-gcc SYSROOT=/absolute/emberbsd-sysroot \
    sh ember/tools/bcm2712-v3d-cl-check-build.sh /absolute/source /absolute/new-output
```

The builder extracts the unchanged, accepted queue-probe packet generator.
Record the actual base headers, libraries and compiler provenance. On CM5,
run `./cl-check /dev/dri/renderD128` with the matched kernel/module bundle.
It checks 325 bin/render pairs, 1,331,200 pixels, command/BO guard regions,
binary fences, two clients and CL/TFU/CL transitions.

## Physical CL acceptance, 2026-10-11

At 13:29:45 UTC, CM5 Rev 1.0 / BCM2712 D0 (4 GiB, 64 GB eMMC) on the
Waveshare CM5-NANO-B passed the checker in 0.55 seconds, exit 0. The existing
UEFI was preserved; its source and carrier PCB revisions remain unknown.
All 325 bin/render pairs, 1,331,200 pixel values, guards, fences, two clients
and 32 TFU copies passed. No engine-error diagnostic appeared in the log.
This short clear/store check is not a shader, Mesa, KMS or desktop result.

GCC 16.2.0 cross-built `EMBERV3DRENDER` from commit
`246b46a2a718b7f4bba24b1e7423f6031fb95c5b` on macOS. Source export,
compiler provenance, ELF/Image/debuglink/CTF and matching module imports
passed verification. The ordinary kernel/module backups remain preserved.
The checker uses the previously recorded partial device ABI inputs; the
installed base revision remains unknown, so this is not full SDK acceptance.

| Artifact | SHA256 |
| --- | --- |
| Kernel ELF | `faa71db5d35c65720d16e9307f6c89a3b42842dd1971ce2ab21638734c8dca30` |
| AArch64 Image | `ea66e528e9b3f95e54c47c6da2ae09972a5209c72bd223d8b0ca580e164ed4ce` |
| Static check | `65d19a58340099f6c3bc957a3b115e0e24537e41e5f153134792a4254308472c` |
| Full kernel/test log | `546ffcb79e91b7b738007efb4dfd4561b0a2676a4096c29e36c1d1e5511b3e31` |

Cppcheck 2.22.0 with actual target preprocessing retained 17 findings each
in the job/driver units, with no new diagnostics against the accepted sync
baseline. GCC 16.2 Analyzer emitted none. Apple Clang 21 retained the same
six inherited header/macro findings. Valgrind is unavailable on these ARM64
hosts; ASan/UBSan covered the host model instead. These tools do not prove
device cache behavior. The hardware pixel/guard check supplies that evidence.

Protocol sources: the existing queue/IRQ probes and their pinned Linux
register references, plus the original Mesa 26.2.4 archive. Preserve licenses
and attribution. The ordinary kernel remains unchanged until an accepted
integration is explicitly installed; experimental bundles are identified.

## Physical Mesa acceptance, 2026-10-11

The matched `EMBERV3DRENDER` build from `47953f7f97c` and Ports
MesaLib 26.2.4nb3/libdrm 2.4.134nb2 pass installed V3D OpenGL on the same
CM5 after normal reboot. Native platform metadata removes the earlier
`eglInitialize` failure. Both explicit render-node and default surfaceless
discovery select `V3D 7.1.10.2`, reporting EGL 1.5 and GLES 3.0.

The consumer rejects invalid GLSL, compiles real vertex/fragment shaders,
checks clear/triangle/outside pixels and destroys objects/context. Four cycles
pass at 16x16 and four at 1280x800; the latter take 0.19 seconds total.
This duration is a short lifecycle check, not a desktop frame-rate benchmark.
All 38 selected Mesa upstream invocations pass, with one internal utility skip
and nine disabled NIR cases. Explicit software comparison also passes four
llvmpipe/LLVM23 cycles. See the Ports
[package receipt and commands](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/mesa-cm5.md).

Kernel ELF SHA256: `889fc52e3b083486414a5473e1626dd035b3093ac295600031d476b6a2872553`.
Image SHA256: `bf3963a0a0eb01714d8065eb6747a4b6ed9702b0a307531eee68cc9edb1c7f91`.
All four companion modules are byte-identical to the accepted CL bundle.
The ordinary bundle remains preserved. This recorded kernel limited
individual BOs to 16 MiB and total allocation to 64 MiB. Current source limits
are defined by the [native allocator contract](bcm2712-v3d-render.md). Sharing buffers between
clients passed the subsequent PRIME gate below. HDMI presentation and the
current common Qt/KWin/Plasma package migration remain subsequent gates.
No long-run desktop claim follows.

## Same-device PRIME integration

The next native interface enables the core GEM handle/fd conversion helpers.
Self-import preserves the actual `bcmv3d_bo`, including its VA and DMA memory.
Foreign-device import has no SG hook and is rejected before attachment. The
exported object has no foreign DMA mapping provider; the common helper now
rejects missing/error providers rather than dereferencing them.

The native fd-mmap callback keeps offsets in bytes within the BO, validates
page alignment, length and read/write protection, and permits shared mappings
only. It holds one GEM reference for the existing CMA pager. It does not use
the generic legacy fake-offset path. Native dma-buf fileops translate callback
errors to positive native errno. Both former error paths fail the actual-source
regression before repair; the new mmap passes 1,260 ASan/UBSan boundary cases.

The native descriptor stores read/write access from `O_ACCMODE`. Unknown
fd flags and the invalid access mode fail before descriptor allocation.
The mmap wrapper rejects protection beyond those access rights before the
provider callback. It caps the provider's maximum protection to the same
rights, so later `mprotect` cannot grant more access. Execute protection
returns `EINVAL`. These descriptor controls have actual-source host tests;
they do not extend the physical acceptance below.

Run `python3 ember/tools/drm-dmabuf-contract.py map` and
`ruby ember/tools/bcm2712-v3d-prime-contract.rb` on the host. Cross-build
`bcm2712-v3d-prime-check-build.sh` with the same `CC`/`SYSROOT` convention as the
CL checker, then run `prime-check /dev/dri/renderD128`. It transfers descriptors
via SCM_RIGHTS to fresh DRM clients, checks duplicate imports, partial mappings,
invalid ranges/protections and lifetime after handle/fd closure. Eighty 1 MiB
cycles exceed the allocation quota cumulatively, detecting retained objects.
The initial hardware failure and the accepted repaired lifecycle are recorded below.

The first physical PRIME run transferred descriptors and validated mappings,
but exhausted the 64 MiB quota before cycle 64. This exposed three inherited
native DMA-BUF lifetime defects: fd export added an unowned reference, the
GEM export-cache cycle was never broken, and final put omitted the exporter's
release callback. Each now has an actual-source failing regression followed
by a passing repair. The callback is mandatory at export, consistent with the
[upstream DMA-BUF lifetime contract](https://docs.kernel.org/driver-api/dma-buf.html).
The other in-tree fd callers (TTM and i915 GVT) also transfer their reference
on success and retain cleanup responsibility on failure.

`python3 ember/tools/drm-dmabuf-contract.py lifetime` exercises the actual export,
fd, put, cache-removal and GEM release functions. It checks allocation failure,
repeated export, 80,000 concurrent get/put pairs, internal/external reservation
objects and exactly one final release under ASan/UBSan. The same Python
standard-library controller runs the map and lifetime cases. `CC` selects
the host compiler; `DRM_SOURCE_ROOT` selects source for causal comparisons.
It preserves both fixtures' sanitizer flags. Missing callbacks are
rejected before allocation. Cppcheck retains 16/16 GEM and 9/9 dma-buf findings
against the previous build; GCC Analyzer adds none.

### Physical PRIME acceptance, 2026-10-11

CM5 booted the matched `EMBERV3DRENDER` build from `78c6715a949`.
At 14:54:15 UTC, all 80 cross-process lifecycles passed in 0.07 seconds,
including 80 MiB cumulative allocation, SCM_RIGHTS, duplicate imports,
nonzero mmap offsets, rejection of private/executable/out-of-range mappings,
and retained mapping after every handle/fd closes. The test exceeds the
64 MiB allocation quota cumulatively; the previous kernel exhausted it.

Unchanged Mesa/libdrm then passed four GBM/PRIME/EGLImage/GLES cycles on
`V3D 7.1.10.2`, comparing all 256 CPU-mapped pixels with GPU readback each
cycle. Four subsequent 1280x800 shader/pixel lifecycles also passed. No
software renderer was permitted. This is same-device linear buffer sharing;
foreign-device imports, arbitrary modifiers and a desktop remain unaccepted.

| Artifact | SHA256 |
| --- | --- |
| Kernel ELF | `e3dd93954fa7fde09e9da0667cb67b7e0c3544e91719be8dfc05e478cd9deea4` |
| Image | `578a5efeac0e3a3e14c9b42ecc5d7852280421ab39ef24a0b7b997ee9fd8f4ef` |
| Static PRIME checker | `8f71c8f0ec53a5ed397d2b7d87d6faa984c45b944a68599923401a92bf8322be` |
| Native PRIME log | `353b67b8142fd31ac8359a86e514601efddb8e92cb5bc23eceb593c278d0fd1c` |
| Mesa PRIME/1280x800 log | `f147b8050b4ad7743ea2d945056ae49855bbf9c4c987913e2194f17851df945e` |

All four modules match the previous accepted Mesa bundle byte for byte;
source-export, compiler, debuglink/CTF and module-symbol checks passed.
The original firmware/base provenance limits and ARM64 Valgrind gap remain.

## Desktop GL and HDMI discovery, 2026-10-11

With the accepted PRIME kernel (`78c6715a949`, ELF
`e3dd93954fa7fde09e9da0667cb67b7e0c3544e91719be8dfc05e478cd9deea4`),
the common Mesa package passes desktop OpenGL 3.1 on V3D 7.1.10.2.
Four 1280x800 shader/pixel/context lifecycles pass for both explicit render-node
and automatic surfaceless selection. All 30 live package libraries match the
cross sysroot; GLES regression, explicit llvmpipe comparison and rejection
controls pass. The [Ports receipt](https://github.com/oxtech-ember/EmberBSD-Ports/blob/84701a6/profiles/common-graphics/cross/mesa-cm5.md#desktop-opengl-2026-10-11)
contains the cross-build instructions, exact test hashes and analysis limits.

A normal reboot with an attached, powered monitor did not expose a framebuffer
in the live FDT or attach wsdisplay. The operator reported no HDMI signal even
before OS startup. Kernel and boot configuration hashes remained unchanged.
This localizes the missing output before desktop startup but does not identify
its cause: firmware initialization, monitor detection and the physical link
still need differentiation. No firmware/EEPROM downgrade was performed.
Rendering is accepted; HDMI/KMS and a complete Plasma session are not.
