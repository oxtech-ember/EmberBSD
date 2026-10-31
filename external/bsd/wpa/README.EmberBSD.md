# Native WPA builds

The native `wpa_supplicant` target uses `EMBER_WPA_SOURCE` to select the
prepared `net/wpa_supplicant-source` tree from Ports. The default
`EMBER_WPA_PROFILE=full` retains the EAP and TLS methods of the regular build.
An opt-in `EMBER_WPA_PROFILE=personal` selects the OpenSSL-backed WPA personal
configuration, with WPA-PSK, SAE, SME and protected management frames; it
does not include enterprise EAP methods or TLS. It requires the OpenSSL build
and rejects conflicting options. WPA's EAPOL four-way handshake remains part
of this profile; the optional IEEE 802.1X EAP state machine is excluded.

The target interface and driver must independently support association, key
installation, SAE external authentication and protected management frames.
Building this profile does not establish that a particular driver supports
those operations. Network credentials are runtime configuration inputs.

Run `tests/personal-profile.py --bmake <native-os-make-wrapper> --source
<prepared-wpa-source>` to check profile selection. Use the SDK native OS
runner and the selected native tool prefix for builds and tests.
