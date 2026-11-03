<!-- Origin: EmberBSD A133 WLAN warm trial, AI-assisted. -->
# A133 WLAN warm trial

`EMBER64_A133_WLAN_WARM` is an explicit test configuration for the
YS-M33 vendor FDT handoff. It is not a cold-boot or release configuration.
It requires the bootloader to leave the WLAN power rail and enable signal
in a usable state. The kernel does not verify that electrical state.

The adapter checks the expected MMC1, PG0–PG5, WLAN_REGON PL5,
WLAN_HOSTWAKE PL6 and AXP803 DLDO1 references before it enables the native
host. It preserves the vendor GPIO binding and does not drive those pins.
The standard `mmc-pwrseq-simple` node has no reset GPIO or external clock:
it represents this preserved-power trial, not a power-on sequence.
A missing, conflicting or malformed profile is not replaced by a default.

This trial explicitly selects the `world-min` policy and channels 1–11.
That conservative selection does not establish a country or product
qualification. The PHY package selector 0 matches the selected table image;
it is not a measurement of the package. The driver still reads the actual
chip EFUSE and rejects invalid calibration data. The configuration does not
supply a copied MAC address or enable HT capabilities.

The fixture checks the FDT transformation in memory. A successful kernel build
or firmware bootstrap does not establish radio, association, WPA2 or physical
board operation. Cold power sequencing requires a separately verified shared
regulator and GPIO ownership contract.
