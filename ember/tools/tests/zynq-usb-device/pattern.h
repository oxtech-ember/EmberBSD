/* SPDX-License-Identifier: BSD-2-Clause */
/* Byte-addressed counter remains deterministic across transfer boundaries. */
static void
usbperiph_counter(uint8_t *buffer, size_t length, uint64_t offset)
{
	for (size_t i = 0; i < length; i++) {
		uint64_t position = offset + i;
		buffer[i] = (uint8_t)((position / 4) >> ((position & 3) * 8));
	}
}
