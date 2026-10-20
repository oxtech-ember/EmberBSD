/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent finite image checks. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_npu_nn.h"
#include "sun60i_a733_npu_nn.c"

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
 fprintf(stderr, "check %u failed at line %d: %s\n", checks, __LINE__, #c); \
 exit(1); } } while (0)

static uint32_t
word(const uint8_t *p)
{
 return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
     (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Decode the two-cycle pipeline delay; flush pairs do not become weights. */
static void
decode(const uint8_t *stream, int values[12])
{
 unsigned pipeline[3][2] = {{0}};
 unsigned emitted = 0;
 for (unsigned cycle = 0; cycle < 8; cycle++) {
  for (unsigned lane = 0; lane < 2; lane++) {
   unsigned bit = (cycle * 2 + lane) * 3;
   unsigned pair = stream[bit / 8] | (unsigned)stream[bit / 8 + 1] << 8;
   unsigned code = pair >> (bit % 8) & 7;
   CHECK(code == 0 || code == 1 || code == 4);
   pipeline[cycle % 3][lane] = code;
  }
  if (cycle >= 2) {
   for (unsigned lane = 0; lane < 2; lane++) {
    unsigned code = pipeline[(cycle + 1) % 3][lane];
    values[emitted++] = code == 4 ? -1 : (int)code;
   }
  }
 }
 CHECK(emitted == 12);
}

static void
verify(const struct sun60i_a733_npu_nn_image *image)
{
 static const uint32_t config[48] = {
  0xa0100106,0x00400200,0xf8000000,0x00400208,
  0x08204008,0x00040084,0x01003000,0x01004000,
  0x00401000,0,0,0,0,0,0,0x30000200,
  0x00400008,8,0,0x03ffffff,0,0x03ffffff,0x00008080,0,
  0,0,0,0x00025000,0,0x7f800000,0xff800000,0,
  0,0,0,0x07ffffff,0,0,0,0,0,0,0,0,0,0,0,0
 };
 static const uint8_t golden[8][6] = {
  {0x41,0x00,0x00,0x00,0x40,0x92}, {0x01,0x02,0x00,0x00,0x40,0x92},
  {0x01,0x10,0x00,0x00,0x40,0x92}, {0x01,0x80,0x00,0x00,0x40,0x92},
  {0x41,0x08,0x00,0x00,0x40,0x92}, {0x01,0x42,0x00,0x00,0x40,0x92},
  {0x01,0x10,0x02,0x00,0x40,0x92}, {0x01,0x81,0x00,0x00,0x40,0x92}
 };
 int decoded[8][12];
 _Static_assert(sizeof(*image) == 1632 && A733_NPU_NN_IMAGE_BYTES == 1632,
     "The fixed image must contain exactly 1632 bytes");
 for (unsigned i = 0; i < 48; i++)
  CHECK(word(image->constants + i * 4) == config[i]);
 for (unsigned i = 192; i < 256; i++) CHECK(image->constants[i] == 0);
 const uint8_t *coef = image->constants + 256;
 CHECK(coef[0] == 0x10);
 for (unsigned i = 1; i < 20; i++) CHECK(coef[i] == 0);
 CHECK(word(coef + 20) == 0x76543210 && word(coef + 24) == 0);
 for (unsigned i = 28; i < 60; i += 4) CHECK(word(coef + i) == 48);
 CHECK(word(coef + 60) == 0);
 for (unsigned core = 0; core < 8; core++) {
  const uint8_t *stream = coef + 64 + 64 * core;
  CHECK(memcmp(stream, golden[core], 6) == 0);
  for (unsigned i = 6; i < 64; i++) CHECK(stream[i] == 0);
  decode(stream, decoded[core]);
  CHECK(decoded[core][0] == 1 && decoded[core][1] == 0);
  for (unsigned i = 6; i < 12; i++) CHECK(decoded[core][i] == 0);
  CHECK(word(coef + 576 + core * 4) == core + 1);
 }
 for (unsigned pixel = 0; pixel < 64; pixel++) {
  /* This arithmetic oracle does not consume the encoded coefficient matrix. */
  int x[4], expected[8];
  for (unsigned channel = 0; channel < 4; channel++) {
   x[channel] = (int)((pixel * (channel + 1) + 3 * channel) % 11) - 5;
   CHECK(image->input[channel * 64 + pixel] == x[channel] + 128);
  }
  expected[0] = x[0] + 1; expected[1] = x[1] + 2;
  expected[2] = x[2] + 3; expected[3] = x[3] + 4;
  expected[4] = x[0] - x[1] + 5; expected[5] = x[1] - x[2] + 6;
  expected[6] = x[2] - x[3] + 7; expected[7] = x[3] - x[0] + 8;
  for (unsigned core = 0; core < 8; core++) {
   int dot = (int)word(coef + 576 + core * 4);
   for (unsigned channel = 0; channel < 4; channel++)
    dot += decoded[core][channel + 2] * x[channel];
   CHECK(dot == expected[core]);
   CHECK(image->expected[core * 64 + pixel] == expected[core] + 128);
   CHECK(image->expected[core * 64 + pixel] != 0xa5);
  }
 }
}

static void
verify_commands(void)
{
 static const uint32_t addresses[18] = {
  0x184,0x3808,0,0xa28,0x384c,0x393c,0x3940,0x3930,0x10a0,
  0x10a4,0x380c,0x380c,0x380c,0x1650,0x3808,0,0x3804,0
 };
 static const uint32_t values[18] = {
  0xffffff7f,0x30000701,0x30000701,0x11,2,0,0,0x10,0x01002000,
  0,0xc23,0xc23,0x23,1,0x701,0x701,0x44,0
 };
 uint8_t commands[144], guarded[144 + 34];
 _Static_assert(A733_NPU_NN_COMMAND_BYTES == 144, "The finite stream is 144 bytes");
 CHECK(sun60i_a733_npu_nn_task_commands(commands, sizeof(commands)) == 0);
 for (unsigned op = 0; op < 18; op++) {
  uint32_t header = word(commands + op * 8);
  CHECK(word(commands + op * 8 + 4) == values[op]);
  if (op == 2 || op == 15) {
   CHECK(header == 0x48000000);
  } else if (op == 17) {
   CHECK(header == 0x10000000);
  } else {
   CHECK(header >> 26 == 2 && (header >> 16 & 0x3ff) == 1);
   CHECK((header & 0xffff) * 4 == addresses[op]);
  }
 }
 CHECK(sun60i_a733_npu_nn_task_commands(NULL, 144) == EINVAL);
 for (size_t bytes = 0; bytes < 144; bytes++) {
  memset(guarded, 0x5a, sizeof(guarded));
  CHECK(sun60i_a733_npu_nn_task_commands(guarded + 1, bytes) == EINVAL);
  for (size_t i = 0; i < sizeof(guarded); i++) CHECK(guarded[i] == 0x5a);
 }
 for (uintptr_t tail = 0; tail < 144; tail++)
  CHECK(sun60i_a733_npu_nn_task_commands((void *)(UINTPTR_MAX - tail), 144) == EINVAL);
 for (size_t align = 0; align < 16; align++) {
  memset(guarded, 0x5a, sizeof(guarded));
  CHECK(sun60i_a733_npu_nn_task_commands(guarded + 1 + align, 144 + 16) == 0);
  CHECK(memcmp(guarded + 1 + align, commands, sizeof(commands)) == 0);
  for (size_t i = 0; i <= align; i++) CHECK(guarded[i] == 0x5a);
  for (size_t i = 1 + align + 144; i < sizeof(guarded); i++) CHECK(guarded[i] == 0x5a);
 }
}

int
main(int argc, char **argv)
{
 uint8_t guarded[1632 + 66];
 struct sun60i_a733_npu_nn_image image;
 CHECK(sun60i_a733_npu_nn_tiny_image(&image, sizeof(image)) == 0);
 if (argc == 2 && strcmp(argv[1], "--image") == 0)
  return fwrite(&image, 1, sizeof(image), stdout) == sizeof(image) ? 0 : 1;
 if (argc == 2 && strcmp(argv[1], "--commands") == 0) {
  uint8_t commands[144];
  CHECK(sun60i_a733_npu_nn_task_commands(commands, sizeof(commands)) == 0);
  return fwrite(commands, 1, sizeof(commands), stdout) == sizeof(commands) ? 0 : 1;
 }
 CHECK(argc == 1);
 verify_commands();
 verify(&image);
 CHECK(sun60i_a733_npu_nn_tiny_image(NULL, 1632) == EINVAL);
 for (size_t bytes = 0; bytes < 1632; bytes++) {
  memset(guarded, 0x5a, sizeof(guarded));
  CHECK(sun60i_a733_npu_nn_tiny_image(guarded + 1, bytes) == EINVAL);
  for (size_t i = 0; i < sizeof(guarded); i++) CHECK(guarded[i] == 0x5a);
 }
 for (uintptr_t tail = 0; tail < 1632; tail++)
  CHECK(sun60i_a733_npu_nn_tiny_image((void *)(UINTPTR_MAX - tail), 1632) == EINVAL);
 for (size_t align = 0; align < 16; align++) {
  for (size_t capacity = 1632; capacity <= 1632 + 16; capacity++) {
   memset(guarded, 0x5a, sizeof(guarded));
   CHECK(sun60i_a733_npu_nn_tiny_image(guarded + 1 + align, capacity) == 0);
   CHECK(memcmp(guarded + 1 + align, &image, sizeof(image)) == 0);
   for (size_t i = 0; i <= align; i++) CHECK(guarded[i] == 0x5a);
   for (size_t i = 1 + align + 1632; i < sizeof(guarded); i++) CHECK(guarded[i] == 0x5a);
  }
 }
 memset(guarded, 0x5a, sizeof(guarded));
 CHECK(sun60i_a733_npu_nn_tiny_image(guarded + 1, SIZE_MAX) == 0);
 CHECK(memcmp(guarded + 1, &image, sizeof(image)) == 0);
 CHECK(guarded[0] == 0x5a && guarded[1633] == 0x5a);
 printf("A733 NPU fixed NN image: %u checks passed\n", checks);
 return 0;
}
