#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Check actual fixed-width ACPI name record layouts.
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'

abort 'Usage: acpi-fixed-names.rb [SOURCE_ROOT [NEW_OUTPUT]]' unless ARGV.length <= 2
root = File.expand_path(ARGV[0] || '../..', ARGV[0] ? Dir.pwd : __dir__)
include_dir = root + '/sys/external/bsd/acpica/dist/include'
local = File.read(include_dir + '/aclocal.h')
types = File.read(include_dir + '/actypes.h')
repair = File.read(root + '/sys/external/bsd/acpica/dist/namespace/nsrepair2.c')
extract = lambda { |source, expression| source[expression] or abort "Missing record declaration: #{expression}" }
name_type = extract.call(local, /^typedef struct acpi_name_info\n\{.*?\} ACPI_NAME_INFO;/m)
simple_type = extract.call(local, /^typedef struct acpi_simple_repair_info\n\{.*?\} ACPI_SIMPLE_REPAIR_INFO;/m)
repair_type = extract.call(repair, /^typedef struct acpi_repair_info\n\{.*?\} ACPI_REPAIR_INFO;/m)
converter = extract.call(local, /^typedef ACPI_STATUS \(\*ACPI_OBJECT_CONVERTER\).*?;/m)
repair_function = extract.call(repair, /^typedef\nACPI_STATUS \(\*ACPI_REPAIR_FUNCTION\).*?;/m)
name_length = extract.call(types, /^#define ACPI_NAMESEG_SIZE[^\n]+/)
packing = extract.call(local, /^#pragma pack\(1\)$/)
fallback = types[/^#ifndef ACPI_NONSTRING\n.*?^#endif/m].to_s
fixture = <<~C
  #include <assert.h>
  #include <stddef.h>
  #include <stdint.h>
  #include <string.h>
  #include <stdarg.h>
  #ifndef TEST_FALLBACK
  #include #{(include_dir + '/platform/acgcc.h').dump}
  #endif
  #{fallback}
  typedef uint8_t UINT8;
  typedef uint16_t UINT16;
  typedef uint32_t UINT32;
  typedef UINT32 ACPI_STATUS;
  struct acpi_namespace_node;
  union acpi_operand_object;
  typedef union acpi_operand_object ACPI_OPERAND_OBJECT;
  typedef struct acpi_evaluate_info ACPI_EVALUATE_INFO;
  #{name_length}
  #{packing}
  #{name_type}
  #pragma pack()
  #{converter}
  #{simple_type}
  #{repair_function}
  #{repair_type}
  _Static_assert(sizeof(((ACPI_NAME_INFO *)0)->Name) == 4, "four-byte predefined name");
  _Static_assert(sizeof(ACPI_NAME_INFO) == 7, "packed predefined record ABI");
  _Static_assert(offsetof(ACPI_NAME_INFO, ArgumentList) == 4, "argument offset");
  _Static_assert(offsetof(ACPI_NAME_INFO, ExpectedBtypes) == 6, "return type offset");
  _Static_assert(sizeof(((ACPI_SIMPLE_REPAIR_INFO *)0)->Name) == 4, "four-byte simple repair name");
  _Static_assert(sizeof(ACPI_SIMPLE_REPAIR_INFO) == 24, "64-bit simple repair record ABI");
  _Static_assert(offsetof(ACPI_SIMPLE_REPAIR_INFO, ObjectConverter) == 16, "converter offset");
  _Static_assert(sizeof(((ACPI_REPAIR_INFO *)0)->Name) == 4, "four-byte complex repair name");
  _Static_assert(sizeof(ACPI_REPAIR_INFO) == 16, "64-bit complex repair record ABI");
  _Static_assert(offsetof(ACPI_REPAIR_INFO, RepairFunction) == 8, "repair function offset");
  #if !defined(TEST_FALLBACK) && defined(__GNUC__) && !defined(__clang__)
  _Static_assert(__builtin_has_attribute(((ACPI_NAME_INFO *)0)->Name, __nonstring__), "predefined name attribute");
  _Static_assert(__builtin_has_attribute(((ACPI_SIMPLE_REPAIR_INFO *)0)->Name, __nonstring__), "simple repair name attribute");
  _Static_assert(__builtin_has_attribute(((ACPI_REPAIR_INFO *)0)->Name, __nonstring__), "complex repair name attribute");
  #endif
  #ifdef TEST_FALLBACK
  #define REG {'_', 'R', 'E', 'G'}
  #define CRS {'_', 'C', 'R', 'S'}
  #define ALR {'_', 'A', 'L', 'R'}
  #else
  #define REG "_REG"
  #define CRS "_CRS"
  #define ALR "_ALR"
  #endif
  static const ACPI_NAME_INFO predefined[] = {{REG, 0x1234, 0x56}, {{0,0,0,0}, 0, 0}};
  static const ACPI_SIMPLE_REPAIR_INFO simple = {CRS, 0x12345678, 0x87654321, NULL};
  static const ACPI_REPAIR_INFO complex_name = {ALR, NULL};
  int main(void) {
    assert(memcmp(predefined[0].Name, "_REG", 4) == 0);
    assert(memchr(predefined[0].Name, 0, 4) == NULL);
    assert(predefined[0].ArgumentList == 0x1234 && predefined[0].ExpectedBtypes == 0x56);
    assert((const char *)&predefined[1] - (const char *)&predefined[0] == 7);
    assert(predefined[1].Name[0] == 0);
    assert(memcmp(simple.Name, "_CRS", 4) == 0 && memchr(simple.Name, 0, 4) == NULL);
    assert(simple.UnexpectedBtypes == 0x12345678 && simple.PackageIndex == 0x87654321);
    assert(simple.ObjectConverter == NULL);
    assert(memcmp(complex_name.Name, "_ALR", 4) == 0 && memchr(complex_name.Name, 0, 4) == NULL);
    assert(complex_name.RepairFunction == NULL);
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
  commands = {}
  { 'supported' => [], 'fallback' => ['-DTEST_FALLBACK'] }.each do |mode, extra|
    commands[mode + '-gcc16'] = [*gcc, *Shellwords.split(ENV.fetch('HOST_GCC16_FLAGS', '')), '-O2', '-std=c17', '-Wall', '-Wextra', '-Werror', '-fanalyzer', *extra, '-c', file, '-o', dir + '/' + mode + '.o']
    commands[mode + '-sanitize-build'] = [*native, *Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer')), *extra, file, '-o', dir + '/' + mode]
    commands[mode + '-sanitize-run'] = [dir + '/' + mode]
  end
  File.write(dir + '/commands.json', JSON.pretty_generate(commands) + "\n")
  commands.each do |phase, command|
    out, err, status = Open3.capture3(*command)
    File.write(dir + '/' + phase + '.log', out + err)
    abort "FAIL: #{phase}: #{out}#{err}" unless status.success?
  end
  puts 'PASS: three actual ACPI record types preserve four-byte names, packed/64-bit layouts and unsupported-compiler fallback'
end
if ARGV[1]
  output = File.expand_path(ARGV[1])
  abort 'Output must be a new directory' if File.exist?(output) || File.symlink?(output)
  Dir.mkdir(output)
  run.call(output)
else
  Dir.mktmpdir('ember-acpi-fixed-', &run)
end
