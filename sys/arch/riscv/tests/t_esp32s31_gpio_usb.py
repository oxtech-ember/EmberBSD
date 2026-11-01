#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual S31 GPIO pad selection and USB-pad ownership handoff."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

source = pathlib.Path(__file__).resolve().parents[1] / "espressif/esp32s31_gpio.c"
text = source.read_text()
out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
fixture = out_dir / "t_esp32s31_gpio_usb.c"
binary = out_dir / "t_esp32s31_gpio_usb"


def function(name):
    match = re.search(r"static (?:int|bool|void \*)\n" + name + r"\([^)]*\)\n\{", text)
    assert match, f"missing {name}"
    depth = 1
    end = match.end()
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[match.start():end]


acquire = function("esp_gpio_acquire")
set_config = function("esp_gpio_set_config")
assert "esp_gpio_usb_pad_available(sc, pin)" in acquire
assert acquire.index("esp_gpio_usb_pad_available(sc, pin)") < acquire.index("mutex_enter(&sc->lock)")
assert "esp_gpio_usb_pad_available(sc, pin)" in set_config
assert set_config.index("esp_gpio_usb_pad_available(sc, pin)") < set_config.index("mutex_enter(&sc->lock)")

header = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#define __BIT(n) (1U << (n))
#define __BITS(h,l) (((~0U) >> (31 - ((h)-(l)))) << (l))
#define __SHIFTIN(v,m) (((v) << 12) & (m))
#define GPIO_PIN(n) (0x0f4 + 4 * (n))
#define GPIO_PAD_OD __BIT(2)
#define USB_SERIAL_JTAG_CONF0 0x018
#define USB_SERIAL_JTAG_USB_PAD_ENABLE __BIT(14)
#define IOMUX_PULLDOWN __BIT(7)
#define IOMUX_PULLUP __BIT(8)
#define IOMUX_INPUT __BIT(9)
#define IOMUX_FUNC __BITS(14, 12)
#define IOMUX_GPIO __SHIFTIN(1, IOMUX_FUNC)
#define PC_INPUT __BIT(0)
#define PC_PULLUP __BIT(2)
#define PC_PULLDOWN __BIT(3)
#define PC_OD __BIT(4)
typedef int bus_space_tag_t;
typedef int bus_space_handle_t;
struct esp_gpio_softc {
  bus_space_tag_t bst;
  bus_space_handle_t gpio, iomux, usb_serial_jtag;
  bool sc_usb_serial_jtag_mapped;
};
static uint32_t gpio_regs[0x1000/4], iomux_regs[0x1000/4], usb_regs[0x1000/4];
static struct { int handle; unsigned offset; uint32_t value; } writes[16];
static unsigned nwrites;
static uint32_t *bank(int handle) {
  if (handle == 1) return gpio_regs;
  if (handle == 2) return iomux_regs;
  assert(handle == 3); return usb_regs;
}
static uint32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, unsigned offset) {
  (void)tag; return bank(handle)[offset/4];
}
static void bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle, unsigned offset, uint32_t value) {
  (void)tag; bank(handle)[offset/4]=value;
  writes[nwrites++] = (typeof(writes[0])){handle, offset, value};
}
'''
main = r'''
int main(void) {
  struct esp_gpio_softc sc={.bst=0,.gpio=1,.iomux=2,.usb_serial_jtag=3,.sc_usb_serial_jtag_mapped=true};
  uint32_t before, iomux_before;
  usb_regs[USB_SERIAL_JTAG_CONF0/4]=0xa5a5ffffU;
  before=usb_regs[USB_SERIAL_JTAG_CONF0/4];
  iomux_before=0xffffffffU;
  iomux_regs[35]=iomux_before;
  gpio_regs[GPIO_PIN(35)/4]=0x83;
  assert(esp_gpio_pad(&sc,35,PC_INPUT|PC_PULLUP)==0);
  assert(nwrites==2 && writes[0].handle==2 && writes[1].handle==1);
  assert(usb_regs[USB_SERIAL_JTAG_CONF0/4]==before);
  assert((iomux_regs[35] & IOMUX_FUNC)==IOMUX_GPIO);
  assert(iomux_regs[35]&IOMUX_INPUT);
  assert(iomux_regs[35]&IOMUX_PULLUP);

  nwrites=0;
  usb_regs[USB_SERIAL_JTAG_CONF0/4]=before|USB_SERIAL_JTAG_USB_PAD_ENABLE;
  assert(esp_gpio_pad(&sc,33,PC_INPUT|PC_OD)==0);
  assert((usb_regs[USB_SERIAL_JTAG_CONF0/4] & USB_SERIAL_JTAG_USB_PAD_ENABLE)==0);
  assert((usb_regs[USB_SERIAL_JTAG_CONF0/4] & ~USB_SERIAL_JTAG_USB_PAD_ENABLE)==
      (before & ~USB_SERIAL_JTAG_USB_PAD_ENABLE));
  assert(nwrites==3 && writes[0].handle==3 && writes[0].offset==USB_SERIAL_JTAG_CONF0);
  assert(writes[1].handle==2 && writes[1].offset==33*4);
  assert(writes[2].handle==1 && writes[2].offset==GPIO_PIN(33));

  nwrites=0;
  sc.sc_usb_serial_jtag_mapped=false;
  for (unsigned pin=33; pin<=34; pin++) {
    assert(!esp_gpio_usb_pad_available(&sc,pin));
    assert(esp_gpio_pad(&sc,pin,PC_INPUT)==ENXIO);
  }
  assert(nwrites==0);
  assert(esp_gpio_usb_pad_available(&sc,35));
  return 0;
}
'''
fixture.write_text(header + function("esp_gpio_usb_pad_available") + "\n" + function("esp_gpio_pad") + main)
subprocess.run(
    [args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
     "-fno-sanitize-recover=all", str(fixture), "-o", str(binary)], check=True
)
subprocess.run([str(binary)], check=True)
print("actual S31 GPIO USB handoff, unaffected pin, and absent-resource ordering passed")
