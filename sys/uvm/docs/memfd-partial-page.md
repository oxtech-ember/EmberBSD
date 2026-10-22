# Partial-page memfd mappings

Origin: EmberBSD; AI-assisted kernel correction and regression tests.

Wayland shared-memory buffers can have a byte length that is not a multiple
of the page size. For example, an 800 by 600 four-byte buffer needs 1920000
bytes. On a system with 4096-byte pages, UVM rounds its mapping to 1921024
bytes before calling the descriptor's mmap operation.

The inherited memfd operation compared that rounded length against the
unrounded file size and returned EINVAL. Truncating the file to a page
boundary hid the error in applications, but changed the allocation size.

The corrected range check admits the final partial page while preserving
the logical file length. The start of the last requested page must remain
before EOF. Comparing distances avoids overflow in offset-plus-length or
page-rounding arithmetic. Negative offsets, empty files and mappings with
a whole page beyond EOF retain their previous rejection. Seal checks and
object reference handling are unchanged.

The [NetBSD mmap manual](https://man.netbsd.org/mmap.2) describes zero-filled
tails of partial pages. This change is limited to that mapping boundary;
it does not implement the broader regular-file SIGBUS behavior for whole
pages beyond EOF or alter truncation semantics for existing mappings.

## Validation

The existing `tests/kernel/t_memfd_create` ATF program now covers:

- shared and private mappings around page boundaries and at a nonzero offset;
- shared writes, zero-filled tails, logical size and EOF preservation;
- mapping lifetime after the descriptor is closed;
- F_SEAL_WRITE and F_SEAL_FUTURE_WRITE on a partial page;
- the existing rejection of invalid mapping ranges.

Run the program through the normal NetBSD test build and `atf-run`.
On the original EMBER64 kernel, the two new partial-page cases fail while
the other fourteen cases pass. This establishes the regression; it is not
a successful run of the corrected kernel.

On 2026-10-07, a clean `EMBER64` build from `aed986038b1` booted on a
physical Orange Pi Zero 3W. The same native ATF executable reproduced the
two failures on the previous kernel and passed all sixteen cases after
the update. The [board receipt](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/README.md)
records the exact kernel identity. This validates the corrected mapping
boundary on that board; it does not update the desktop VM's running kernel.

Live acceptance requires a completely rebuilt kernel, the same sixteen
ATF cases, and a Wayland allocation test without application-side rounding.
Remove the temporary KWin allocation workaround only after those native
checks pass. An individual compiled kernel object is not boot validation.
