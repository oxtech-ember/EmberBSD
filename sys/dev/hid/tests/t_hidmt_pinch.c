/* SPDX-License-Identifier: BSD-2-Clause */
/* Native two-finger pinch regression on the real gesture policy. */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>
size_t hidmt_test_allocations;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
int main(void)
{
 struct hidmt_gstate g = {0};
 struct hidmt_gconf gc = { .enable = true };
 struct hidmt_motion m = {0};
 struct hidmt_frame f = { .ncontacts = 2, .contacts = {{.id=1,.x=1000,.y=1000},{.id=2,.x=2000,.y=1000}} };
 hidmt_gesture_update(&g, &gc, &f, &m);
 CHECK(m.gesture == 0);
 f.contacts[0].x = 800; f.contacts[1].x = 2200;
 memset(&m, 0, sizeof(m));
 hidmt_gesture_update(&g, &gc, &f, &m);
 CHECK((m.gesture & HIDMT_GESTURE_BEGIN) != 0);
 CHECK(m.gesture_fingers == 2);
 puts("pinch begin: PASS");
 return 0;
}
