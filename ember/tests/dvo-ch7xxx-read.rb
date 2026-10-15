# SPDX-License-Identifier: BSD-2-Clause
# Exercise the actual CH7xxx register consumers with failed I2C transfers.
require 'tmpdir'
require 'open3'
require 'shellwords'

root = File.expand_path('../..', __dir__)
path = ARGV[0] || root + '/sys/external/bsd/drm2/dist/drm/i915/display/dvo_ch7xxx.c'
source = File.read(path)
names = %w[ch7xxx_detect ch7xxx_mode_set ch7xxx_get_hw_state ch7xxx_dump_regs]
functions = names.map do |name|
  start = source.index(/^static [^\n]*\b#{name}\(/) or abort "Missing #{name}"
  opening = source.index('{', start)
  depth = 1
  finish = opening + 1
  while depth > 0
    case source[finish]
    when '{' then depth += 1
    when '}' then depth -= 1
    end
    finish += 1
    abort 'Unclosed function' if finish >= source.length
  end
  source[start...finish]
end.join("\n")
macros = source.lines.grep(/^#define CH7xxx_/).join
fixture = source[0...source.index('#include <sys/cdefs.h>')] + <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <stdint.h>
  #include <stdio.h>
  #include <stdarg.h>
  typedef uint8_t u8;
  struct intel_dvo_device { int unused; };
  struct drm_display_mode { int clock, flags; };
  enum drm_connector_status { connector_status_unknown,
    connector_status_connected, connector_status_disconnected };
  #define DRM_MODE_FLAG_PHSYNC 1
  #define DRM_MODE_FLAG_PVSYNC 2
  #{macros}
  static int fail_read = -1, reads, writes, fail_write = -1;
  static u8 values[256], last_value[256];
  static int register_writes[256];
  static bool ch7xxx_readb(struct intel_dvo_device *dvo, int reg, u8 *out) {
    (void)dvo; reads++;
    if (reg == fail_read) { *out = 0xff; return false; }
    *out = values[reg]; return true;
  }
  static bool ch7xxx_writeb(struct intel_dvo_device *dvo, int reg, u8 value) {
    (void)dvo; writes++; last_value[reg] = value; register_writes[reg]++;
    return writes != fail_write;
  }
  static void debug_log(const char *format, ...) { (void)format; }
  #define DRM_DEBUG_KMS(...) debug_log(__VA_ARGS__)
  #{functions}
  static void reset(void) {
    fail_read = fail_write = -1; reads = writes = 0;
    for (unsigned i = 0; i < 256; i++) {
      values[i] = last_value[i] = 0; register_writes[i] = 0;
    }
  }
  int main(void) {
    struct intel_dvo_device dvo = {0};
    reset(); fail_read = CH7xxx_PM;
    assert(!ch7xxx_get_hw_state(&dvo));
    assert(ch7xxx_detect(&dvo) == connector_status_unknown);
    assert(writes == 0);
    reset(); values[CH7xxx_PM] = CH7xxx_PM_DVIL;
    assert(ch7xxx_get_hw_state(&dvo));
    values[CH7xxx_PM] = CH7xxx_PM_DVIP;
    assert(ch7xxx_get_hw_state(&dvo));
    values[CH7xxx_PM] = CH7xxx_PM_FPD;
    assert(!ch7xxx_get_hw_state(&dvo));
    reset(); values[CH7xxx_PM] = CH7xxx_PM_FPD;
    fail_read = CH7xxx_CONNECTION_DETECT;
    assert(ch7xxx_detect(&dvo) == connector_status_unknown);
    assert(writes == 2 && last_value[CH7xxx_PM] == CH7xxx_PM_FPD);
    for (int fail = 1; fail <= 2; fail++) {
      reset(); fail_write = fail; values[CH7xxx_PM] = CH7xxx_PM_FPD;
      assert(ch7xxx_detect(&dvo) == connector_status_unknown);
      assert(writes == 2 && last_value[CH7xxx_PM] == CH7xxx_PM_FPD);
    }
    for (int connected = 0; connected <= 1; connected++) {
      reset(); values[CH7xxx_PM] = CH7xxx_PM_FPD;
      values[CH7xxx_CONNECTION_DETECT] = connected ? CH7xxx_CDET_DVI : 0;
      assert(ch7xxx_detect(&dvo) == (connected ? connector_status_connected : connector_status_disconnected));
      assert(writes == 2 && last_value[CH7xxx_PM] == CH7xxx_PM_FPD);
    }
    struct drm_display_mode mode = {65000, DRM_MODE_FLAG_PHSYNC};
    reset(); fail_read = CH7xxx_IDF;
    ch7xxx_mode_set(&dvo, &mode, &mode);
    assert(register_writes[CH7xxx_IDF] == 0);
    reset(); values[CH7xxx_IDF] = CH7xxx_IDF_VSP | 1;
    ch7xxx_mode_set(&dvo, &mode, &mode);
    assert(last_value[CH7xxx_IDF] == (CH7xxx_IDF_HSP | 1));
    reset(); fail_read = 3; ch7xxx_dump_regs(&dvo);
    assert(reads == CH7xxx_NUM_REGS);
    puts("PASS: CH7xxx read errors and power-state restoration");
  }
C
Dir.mktmpdir('ember-ch7xxx-') do |dir|
  file = dir + '/test.c'
  File.write(file, fixture)
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  # Match the kernel's unused-parameter policy for the upstream callbacks.
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O2 -g -std=c17 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined'))
  out, err, status = Open3.capture3(*cc, *flags, file, '-o', dir + '/test')
  abort out + err unless status.success?
  out, err, status = Open3.capture3(dir + '/test')
  abort out + err unless status.success?
  print out
end
