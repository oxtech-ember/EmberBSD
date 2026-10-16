#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Exercise the actual aligned ACPI name macro.
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'

abort 'Usage: acpi-aligned-name.rb [SOURCE_ROOT [NEW_OUTPUT]]' unless ARGV.length <= 2
root = File.expand_path(ARGV[0] || '../..', ARGV[0] ? Dir.pwd : __dir__)
header = File.read(root + '/sys/external/bsd/acpica/dist/include/actypes.h')
names = File.read(root + '/sys/external/bsd/acpica/dist/include/acnames.h')
type = header[/^typedef UINT32\s+ACPI_NAME;[^\n]*/] or abort 'Missing ACPI name type'
macro = header[/^#define ACPI_ALIGNED_STR_UNION\(t, s\)[^\n]*\\\n[^\n]+/] or abort 'Missing aligned name macro'
name = names[/^#define METHOD_NAME__REG[^\n]+/] or abort 'Missing _REG name'
fixture = <<~C
  #include <assert.h>
  #include <stdint.h>
  #include <string.h>
  typedef uint32_t UINT32;
  #{type}
  #{macro}
  #{name}
  _Static_assert(sizeof(ACPI_NAME) == 4, "namespace lookup still consumes four bytes");
  int main(void) {
    const ACPI_NAME *name = ACPI_ALIGNED_STR_UNION(ACPI_NAME, METHOD_NAME__REG);
    uint32_t expected;
    memcpy(&expected, "_REG", 4);
    assert((uintptr_t)name % _Alignof(ACPI_NAME) == 0);
    assert(*name == expected);
    assert(memcmp(name, "_REG", 4) == 0);
    assert(((const char *)name)[4] == 0);
    return 0;
  }
C
run = lambda do |dir|
  file = dir + '/fixture.c'
  File.write(file, fixture)
  gcc = Shellwords.split(ENV.fetch('HOST_GCC16', 'gcc-16'))
  version, err, status = Open3.capture3(*gcc, '-dumpfullversion')
  abort "Select host GCC16: #{version}#{err}" unless status.success? && version.start_with?('16.')
  native = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  commands = {
    'gcc16' => [*gcc, *Shellwords.split(ENV.fetch('HOST_GCC16_FLAGS', '')), '-O2', '-std=c17', '-Wall', '-Wextra', '-Werror', '-fanalyzer', '-c', file, '-o', dir + '/fixture.o'],
    'sanitize-build' => [*native, *Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer')), file, '-o', dir + '/fixture'],
    'sanitize-run' => [dir + '/fixture']
  }
  File.write(dir + '/commands.json', JSON.pretty_generate(commands) + "\n")
  commands.each do |phase, command|
    out, err, status = Open3.capture3(*command)
    File.write(dir + '/' + phase + '.log', out + err)
    abort "FAIL: #{phase}: #{out}#{err}" unless status.success?
  end
  puts 'PASS: aligned ACPI name preserves four bytes, typed value, alignment and local terminator storage'
end
if ARGV[1]
  output = File.expand_path(ARGV[1])
  abort 'Output must be a new directory' if File.exist?(output) || File.symlink?(output)
  Dir.mkdir(output)
  run.call(output)
else
  Dir.mktmpdir('ember-acpi-name-', &run)
end
