# Cooperative vnode iteration

`vfs_vnode_iterator_next1()` checks for pending rescheduling before taking
iterator locks and after each batch of 128 rejected entries. Before releasing
the mount list lock, it parks the existing iterator marker after the last
examined entry. It resumes from that marker after reacquiring the lock; vnode
pointers are not retained across the scheduling point. Selected vnode
references and `vcache_vget()` retry semantics remain unchanged.

This bounds the number of rejected entries scanned without a scheduling
point, not elapsed time. Selector cost, lock contention, and the work performed
by callers still affect scheduling latency. The checkpoint does not force a
switch when no reschedule is pending.

## Host checks

The runner extracts the four iterator functions from `sys/kern/vfs_mount.c`.
The fixture supplies controlled vnode, lock and scheduler operations; it does
not maintain a second implementation of the iterator.

```sh
sh ember/tools/tests/vnode-iterator/run.sh
MEMCHECK=yes sh ember/tools/tests/vnode-iterator/run.sh
ANALYZE=yes sh ember/tools/tests/vnode-iterator/run.sh
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/tests/vnode-iterator/run.sh
```

`CC` selects the host C compiler. `ANALYZE=yes` requires Cppcheck. An optional source-file argument supports a negative check
against the original unbounded implementation; that implementation must
compile and then fail the scan-bound assertion.

Cases cover empty and boundary-length scans, long rejected and dead-vnode
lists, selected references, marker skipping, early destruction, reclamation
between selection and reference acquisition, removal of both neighbours,
insertion after the parked marker, removal of the remaining tail, and a
second iterator running at the checkpoint. Lock assertions verify that
checkpoints execute outside both iterator interlocks. These deterministic
checks do not replace target kernel builds and scheduler traces on hardware.
