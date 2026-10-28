# Verified firmware source cache

`build-firmware.sh` keeps the pinned Git checkout and its build outputs.
It does not reset or clean an existing checkout. It does not delete its
ignored or untracked files. A changed pin requires an explicit source task.
A newly created checkout can populate its pinned submodules.

`--prepare` needs Git, Ruby and `patch`. It verifies and applies the ordered
series without a target compiler or a firmware build. It preserves accepted
variant outputs. The source checker exports only referenced pinned Git blobs
into a private output directory. It applies patches to that export.
It publishes only verified changed source files to the cache.

The builder and the standalone source helper use the same cache-wide mkdir
lease. The builder holds it through preparation, upstream build, the upstream
temporary SD patch cleanup, source postcheck and image publication.
The helper accepts an explicit direct-parent lease from the builder.
An environment variable cannot bypass the lease. A stale or changed lease
causes a refusal. Inspect and preserve it before any manual recovery.

## First use and legacy adoption

A cache without a source manifest must match the pristine pinned source.
The `.ember-pinned` and `.ember-series` markers are not proof of its contents.
For an existing patched cache, supply its exact previous OS recipe commit.
Keep the accepted image, receipt and invocation outside the selected rebuild
output before adoption or a rebuild. A rebuild still invalidates that
selected old output before tool, source and build checks.

An existing object tree also needs the previous successful build receipt
and invocation at initial adoption. The helper checks their recipe, cache
path, variant, compiler identity, profile and specs against immutable OS
blobs. It does not infer old inputs from current files.

```sh
ruby ember/tools/firmware-source.rb \
    /absolute/cache/rpi5-uefi-src /absolute/empty-adoption-output \
    --previous-ref FULL_PREVIOUS_OS_COMMIT \
    --previous-build-receipt /absolute/saved/firmware-receipt.json \
    --previous-build-invocation /absolute/saved/build-invocation.json
```

This command only checks and saves a plan. Add `--apply` after review.
Do not use a receipt from another cache. Do not invent missing provenance.
The output parent must exist. SOURCE and output must not overlap.

## Source and object contracts

The versioned source manifest records pins, recipe and patch hashes, target
exact modes, ordered series, original hashes, final hashes and the submodule graph. The helper
rejects unknown tracked or staged changes, unsupported index flags, changed
pins, target collisions, unsafe types and symlink parents. Unregistered but
populated submodules cannot be skipped. Other untracked files stay in place.

The helper verifies all targets before it writes. Each replacement uses an
atomic rename. The whole batch is not atomic. A pending record and exact
old source bytes remain if publication stops. A pending batch cannot build.
This version has no automatic recovery. Preserve its work directory and
resolve it in a separate source task. Removal of a patch-created file is
not supported. It causes a refusal with no source change.

TF-A does not track build-option changes. The option-profile check is
separate from the source manifest. It compares the saved variant/options,
compiler family, path, version and target, selected freestanding flags,
compiler headers and prepared codec tree identities
and pinned Conf templates. The explicit GCC profile also
checks its specs and prefix. Only typed empty-search-root and
specs-path roles are normalized. Their current directories and hashes must
pass the normal compiler profile checks. Existing custom Conf inputs need
an explicit supported contract. They are not overwritten by preparation.
An incompatible option profile causes a refusal and preserves the objects.
This is not proof of historic binutils or compiler-header content hashes.
Normal source/header dependencies still apply. Do not restore old mtimes.

Upstream builds update their normal products. BaseTools runs its normal
`all` target and tests. EDK2 preserves existing Conf text files without
`--reconfig` and regenerates its `BuildEnv.sh`. The pinned top-level script
applies and reverses its temporary SD patch. The wrapper verifies source
state after it exits. A failed restore or a changed manifest prevents image
publication. Cleanup failures invalidate the owned output before unlock.
A lost lease causes refusal and preserves potentially foreign outputs.
The wrapper never changes shared outputs after a successful unlock.

## Source controls

```sh
ruby ember/tools/firmware-source-contract.rb /absolute/empty-controls-output
ruby ember/tools/firmware-contract.rb
```

The first runner uses the actual helper and bounded real Git fixtures.
The second uses controlled services, including the source-helper boundary,
to check the real builder's orchestration and failure handling. Neither
runner builds firmware or accesses a device. Full firmware build and
physical acceptance remain separate checks.
