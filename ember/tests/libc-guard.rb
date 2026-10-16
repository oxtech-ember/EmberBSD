#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted regression for the libc canary initializer.
require 'fileutils'
require 'open3'
require 'json'
abort 'Usage: libc-guard.rb SOURCE HOST_GCC NEW_OUTPUT' unless ARGV.size == 3
src, cc = ARGV.first(2).map { |p| File.realpath(p) }
out = File.expand_path(ARGV[2])
abort 'Output must not exist' if File.exist?(out)
FileUtils.mkdir_p(out)
text = File.read(src + '/lib/libc/misc/stack_protector.c')
body = text[/void __section\("\.text\.startup"\).*?(?=\/\*ARGSUSED\*\/)/m]
abort 'Cannot extract actual guard initializer' unless body
fixture = <<~C
  #include <stddef.h>
  #include <string.h>
  #include <stdlib.h>
  #include <unistd.h>
  #define __section(x)
  #define CTL_KERN 1
  #define KERN_ARND 81
  #define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
  typedef unsigned int u_int;
  long __stack_chk_guard[8];
  static int mode;
  static int calls;
  void __guard_setup(void);
  void __stack_chk_fail(void);
  int __sysctl(const int *, u_int, void *, size_t *, const void *, size_t);
  __attribute__((no_stack_protector, noreturn)) void
  __stack_chk_fail(void) { _exit(99); }
  __attribute__((no_stack_protector, noinline)) int
  __sysctl(const int *mib, u_int n, void *p, size_t *len,
      const void *unused, size_t zero)
  {
      (void)unused;
      if (n != 2 || mib[0] != CTL_KERN || mib[1] != KERN_ARND ||
          *len != 64 || zero != 0) _exit(98);
      ++calls;
      if (mode == 1) return -1;
      memset(p, 0x5a, *len);
      if (mode == 2) *len = 1;
      return 0;
  }
  #{body}
  __attribute__((noinline)) static void
  protected_frame(int corrupt)
  {
      volatile char buffer[16];
      buffer[0] = 1;
      if (corrupt) __stack_chk_guard[0] ^= 1;
      __asm__ volatile("" : : "r"(&buffer) : "memory");
  }
  __attribute__((no_stack_protector)) int
  main(int argc, char **argv)
  {
      if (argc != 2) return 97;
      mode = atoi(argv[1]);
      if (mode == 3) __stack_chk_guard[0] = 42;
      __guard_setup();
      if (mode == 3) return calls != 0 || __stack_chk_guard[0] != 42;
      if (calls != 1) return 96;
      if (mode == 1 || mode == 2) {
          const unsigned char *p = (const unsigned char *)__stack_chk_guard;
          if (p[0] != 0 || p[1] != 0 || p[2] != 10 || p[3] != 255)
              return 95;
      } else if (__stack_chk_guard[0] != 0x5a5a5a5a5a5a5a5aL) return 94;
      __guard_setup();
      if (calls != 1) return 93;
      protected_frame(mode == 4);
      return 0;
  }
C
File.write(out + '/fixture.c', fixture)
args = [cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
  '-fstack-protector-strong', '-mstack-protector-guard=global',
  out + '/fixture.c', '-o', out + '/fixture']
args += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if ENV['LIBC_GUARD_ASAN'] == 'yes'
stdout, stderr, status = Open3.capture3(*args)
File.write(out + '/compile.log', stdout + stderr)
abort "Compile failed: #{stderr}" unless status.success?
results = (0..4).map do |mode|
  stdout, stderr, status = Open3.capture3(out + '/fixture', mode.to_s)
  expected = mode == 4 ? 99 : 0
  {mode: mode, expected: expected, exit: status.exitstatus,
    signal: status.termsig, stdout: stdout, stderr: stderr}
end
File.write(out + '/results.json', JSON.pretty_generate(results) + "\n")
results.each do |r|
  abort "FAIL: mode #{r[:mode]} expected #{r[:expected]}, got #{r[:exit]}" unless r[:exit] == r[:expected]
end
puts 'PASS: entropy, syscall failure, short read, preinitialized guard, repeat and live SSP failure'
