# Origin: EmberBSD quiet kernel banner regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
source = File.read(root + '/sys/kern/init_main.c')
prf = File.read(root + '/sys/kern/subr_prf.c')
banner = source[/void\nbanner\(void\)\n\{.*?^\}/m] or abort 'Missing kernel banner'
normal = prf[/static void\naprint_normal_internal\(.*?^\}/m] or abort 'Missing kernel log routing'
normal += "\n" + (prf[/void\naprint_normal\(.*?^\}/m] or abort 'Missing normal printer')
code = <<~C
  #include <assert.h>
  #include <stdarg.h>
  #include <stdbool.h>
  #include <stdint.h>
  #include <stdio.h>
  #include <string.h>
  #ifndef __printflike
  #define __printflike(a,b)
  #endif
  #define AB_SILENT 1
  #define AB_QUIET 2
  #define AB_VERBOSE 4
  #define TOCONS 1
  #define TOLOG 2
  #define ctob(x) ((x) * 4096)
  #{source[/^#define MEM_PBUFSIZE.*$/]}
  static int boothowto;
  static const char *panicstr, *ostype = "NetBSD", *osrelease = "11.0";
  static const char *kernel_ident = "GENERIC";
  static const char *copyright = "Copyright upstream notice";
  static const char *version = "NetBSD 11.0 diagnostic version";
  static uint64_t physmem = 1024;
  static char console_out[4096], log_out[4096];
  static void kprintf_lock(void) {}
  static void kprintf_unlock(void) {}
  static void logwakeup(void) {}
  static void kprintf(const char *fmt, int flags, void *a, void *b, va_list ap) {
    char text[1024]; (void)a; (void)b;
    vsnprintf(text, sizeof(text), fmt, ap);
    if (flags & TOCONS) strncat(console_out, text, sizeof(console_out)-strlen(console_out)-1);
    if (flags & TOLOG) strncat(log_out, text, sizeof(log_out)-strlen(log_out)-1);
  }
  static void kprintf_internal(const char *fmt, int flags, void *a, void *b, ...) {
    va_list ap; va_start(ap, b); kprintf(fmt, flags, a, b, ap); va_end(ap);
  }
  static void printf_nolog(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); kprintf(fmt, TOCONS, NULL, NULL, ap); va_end(ap);
  }
  static void kernel_printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); kprintf(fmt, TOCONS|TOLOG, NULL, NULL, ap); va_end(ap);
  }
  static uint64_t uvm_availmem(bool unused) { (void)unused; return 2048; }
  static void format_bytes(char *out, size_t n, uint64_t count) { snprintf(out, n, "%llu", (unsigned long long)count); }
  #{normal}
  #define printf kernel_printf
  #{banner}
  int main(void) {
    boothowto = AB_SILENT;
    banner();
    assert(strcmp(console_out, TEST_EXPECTED) == 0);
    assert(strstr(log_out, copyright) != NULL);
    assert(strstr(log_out, version) != NULL);
    assert(strstr(log_out, "total memory") != NULL);
    console_out[0] = log_out[0] = 0;
    boothowto = AB_VERBOSE;
    banner();
    assert(strstr(console_out, copyright) != NULL);
    assert(strstr(console_out, version) != NULL);
    assert(strstr(log_out, copyright) != NULL);
    return 0;
  }
C
Dir.mktmpdir('ember-kernel-banner') do |work|
  File.write(work + '/banner.c', code)
  {'1.0' => "EmberBSD 1.0 (GENERIC)\n", '2.7' => "EmberBSD 2.7 (GENERIC)\n"}.each do |product, expected|
    output, status = Open3.capture2e('cc', '-std=c99', '-fsanitize=address,undefined',
      '-g', '-DEMBER_VERSION=' + product.dump, '-DTEST_EXPECTED=' + expected.dump,
      work + '/banner.c', '-o', work + '/banner')
    abort output unless status.success?
    abort "Quiet kernel banner failed for #{product}" unless system(work + '/banner')
  end
end
puts 'Quiet kernel product banner and retained diagnostic/log attribution pass ASan/UBSan'
