# Checked SDIO interrupt operations

The checked interrupt APIs return host-command and card R5 errors.
CRC errors return `EILSEQ`; other R5 error flags return `EIO`.
The shared decoder is in `sdmmc_io_status.h`.
CMD52 reads publish their byte only after both checks succeed.

`sdmmc_intr_enable_checked(sf)` and `sdmmc_intr_disable_checked(sf)` update one IEN bit.
They preserve other functions' bits and verify the requested bit by readback.
They serialize the complete update with the host's `sc_mtx`.
A caller already holding that mutex does not acquire it again.
The existing void wrappers retain their signatures and discard the returned errno.
Direct packet I/O remains non-locking for handlers that already hold `sc_mtx`.

`sdmmc_intr_establish_checked(dev, handler, arg, name, &cookie)` registers host routing.
It changes the output cookie only after successful registration.
The first registration enables the master IEN bit before enabling the host source.
Failed registration attempts release only a master bit they attempted to acquire.
Cleanup preserves the original error, including after an uncertain write.
The function IEN bit remains the function driver's responsibility.

The registry tracks ownership of a master bit that was initially clear.
That ownership moves to a remaining handler when its owner unregisters.
The last handler disables the host source and releases only an owned master bit.
A master bit that was already enabled is preserved.
Every card update reads the current IEN byte and preserves unrelated bits.

`sdmmc_intr_disestablish_checked(cookie)` consumes a valid cookie even if card I/O fails.
It removes the handler and disables the last host route before returning that error.
Do not retry with the consumed cookie. A null cookie succeeds without work.
Registration and removal must run outside the host interrupt callback.
The registry holds `sc_mtx` while invoking handlers; those callbacks should defer work.

`ember/tools/sdmmc-irq-contract.py SOURCE OUTPUT` tests the production functions.
Select the SDK Python and `HOST_CC` explicitly.
The test covers R5 errors, unchanged outputs, IEN ownership, allocation failures,
uncertain writes, shared routing and teardown errors.
The RTW SDIO and diagnostic contracts also exercise the shared R5 decoder.
These checks do not prove physical interrupt delivery.
