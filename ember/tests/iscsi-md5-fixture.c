/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; iSCSI MD5 known-vector regression. */
#include <stdint.h>
#include <string.h>
#include "iscsi-md5.h"
int main(void) {
 char out[33];
 if (strcmp(iSCSI_MD5Data((const uint8_t *)"", 0, out), "d41d8cd98f00b204e9800998ecf8427e")) return 1;
 if (strcmp(iSCSI_MD5Data((const uint8_t *)"abc", 3, out), "900150983cd24fb0d6963f7d28e17f72")) return 2;
 return 0;
}
