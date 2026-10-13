/* Origin: EmberBSD - test native and legacy GPIO cell decoding. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "../../sys/arch/arm/sunxi/sunxi_gpio_cells.h"
int main(void) {
 uint8_t native[16] = {0}, legacy[28] = {0}, port, pin; bool actlo;
 native[7]=5; native[11]=2; native[15]=1;
 assert(sunxi_gpio_cells(native,16,&port,&pin,&actlo));
 assert(port==5 && pin==2 && actlo);
 legacy[7]=5; legacy[11]=2; legacy[15]=1; legacy[27]=1;
 assert(sunxi_gpio_cells(legacy,28,&port,&pin,&actlo));
 assert(port==5 && pin==2 && !actlo);
 legacy[15]=7; assert(!sunxi_gpio_cells(legacy,28,&port,&pin,&actlo));
 assert(!sunxi_gpio_cells(native,12,&port,&pin,&actlo));
 puts("Sunxi GPIO cells: native polarity, legacy output polarity and malformed cells passed");
 return 0;
}
