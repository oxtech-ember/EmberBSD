# AIC8800D80 BIP verification helper

The internal `if_aicwf_bip.h` helper verifies BIP-CMAC-128 management frames.
It is software preparation for host PMF processing. The driver does not call
it yet. It does not remove the nonzero initial IGTK IPN restriction.
See [SAE support and limits](aicwf-sae.md).

## Contract

The caller supplies a complete management MPDU without its FCS. The frame
must include its final 18-byte MMIE. The maximum frame length is 1536 bytes.
The caller selects the IGTK, expected source, BSSID, KeyID and previous IPN.
Key IDs 4 and 5 are supported. The initial IPN uses the same comparison as
the last accepted IPN. The received value must be greater.

The helper accepts group-addressed deauthentication, disassociation and
RRM/WNM action frames. Deauthentication and disassociation require a
2-byte reason body. Other action categories, optional headers, fragmented
frames and extended disconnect bodies are unsupported. The caller must
validate action-specific fields after authentication.

An AES-128 block callback supplies the cipher operation. A kernel adapter
uses the existing `aes_setenckey128()` and `aes_enc()` interfaces. The helper
contains no AES implementation and introduces no kernel OpenSSL dependency.
CMAC uses at most three bounded segments. It compares all eight MIC bytes.
It clears its intermediate authentication material before returning.

A successful result supplies a candidate packet number. The caller must
commit that value under the key and association lifetime lock. The caller
must preserve replay state on duplicate key installation. Failure leaves
the candidate unchanged. The helper does not install keys or change state.

## Host contract

The test requires a C11 compiler, `pkg-config` and OpenSSL 3 or later.
OpenSSL supplies actual AES blocks and a separate EVP_MAC CMAC oracle.
The test also checks all four RFC 4493 CMAC vectors. It covers segment
boundaries, complete frame lengths, truncation, replay, addresses, header
formats, key selection and MIC corruption.

```sh
sh ember/tests/aicwf-bip.sh
AICWF_BIP_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all' \
    sh ember/tests/aicwf-bip.sh
```

The runner makes sanitizer findings fail the run. Use an external `TMPDIR`
when required by the development host. The test is not a radio test.

## Required firmware evidence

The pinned Radxa protocol has no initial IGTK IPN field in `MM_KEY_ADD_REQ`.
Its RX descriptor has no separate BIP verification result. The known vendor
host path preserves group management uploads, but that does not prove that
the firmware preserves the complete MMIE.

Before integration, verify the received frame and MMIE on physical hardware.
Check fresh frames, repeats, values below the initial IPN and invalid MICs.
Repeat the check after IGTK reinstallation. A firmware disconnect indication
contains no authenticated frame or packet number. Establish that the firmware
does not retire the connection before host rejection. An ignored indication
does not reverse a radio disconnect. RX filter changes alone do not establish
this property.

Unicast protected k/v actions use the pairwise CCMP path. This BIP helper
does not replace that path or validate transmit acknowledgements.

## References

- [RFC 4493: AES-CMAC and test vectors](https://www.rfc-editor.org/rfc/rfc4493)
- [Radxa SDIO protocol source, pinned revision](https://github.com/radxa-pkg/aic8800/tree/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv)

The helper is an independent BSD implementation. No GPL driver code was copied.
