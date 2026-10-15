#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Validate the real GPU0 ASL component and feed its resources to the OS decoder.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'

abort 'usage: bcm2712-display-firmware-contract.rb PATCHED_EDK2_PLATFORMS [OS_SOURCE]' unless (1..2).cover?(ARGV.length)
platform = File.expand_path(ARGV[0])
os = File.expand_path(ARGV[1] || '../..', __dir__)
iasl = Shellwords.split(ENV.fetch('IASL', 'iasl'))
cc = Shellwords.split(ENV.fetch('CC', 'cc'))
cflags = Shellwords.split(ENV.fetch('CFLAGS', ''))
COMMANDS = []

def run(*command)
  output, status = Open3.capture2e(*command)
  COMMANDS << { arguments: command, exit_status: status.exitstatus }
  puts output unless output.empty?
  abort "command failed: #{command.shelljoin}" unless status.success?
end

def package_length(bytes, offset)
  first = bytes.getbyte(offset) or raise 'missing package length'
  extra = first >> 6
  length = extra.zero? ? first & 63 : first & 15
  extra.times do |i|
    byte = bytes.getbyte(offset + i + 1) or raise 'truncated package length'
    length |= byte << (4 + 8 * i)
  end
  raise 'invalid package length' if length < extra + 1 || offset + length > bytes.bytesize
  [length, extra + 1]
end

def constant(bytes, offset)
  first = bytes.getbyte(offset)
  return [first, offset + 1] if first == 0 || first == 1
  width = { 10 => 1, 11 => 2, 12 => 4, 14 => 8 }[first]
  raise 'expected AML integer constant' unless width
  data = bytes[offset + 1, width]
  raise 'truncated AML integer' unless data && data.bytesize == width
  [data.bytes.each_with_index.sum { |v, i| v << (8 * i) }, offset + width + 1]
end

def object(bytes, offset)
  opcode = bytes.getbyte(offset)
  if opcode == 0x12
    length, count = package_length(bytes, offset + 1)
    finish = offset + 1 + length
    pos = offset + 1 + count
    elements = bytes.getbyte(pos) or raise 'missing package count'
    pos += 1
    values = elements.times.map do
      value, pos = object(bytes, pos)
      value
    end
    raise 'package boundary mismatch' unless pos == finish
    [values, finish]
  elsif opcode == 0x11
    length, count = package_length(bytes, offset + 1)
    finish = offset + 1 + length
    size, pos = constant(bytes, offset + 1 + count)
    raise 'buffer boundary mismatch' unless pos + size == finish
    [{ buffer: bytes[pos, size] }, finish]
  elsif opcode == 0x0d
    finish = bytes.index("\0", offset + 1) or raise 'unterminated AML string'
    [bytes[offset + 1...finish], finish + 1]
  else
    constant(bytes, offset)
  end
end

def consume(bytes, offset, expected)
  raise "unexpected AML instruction at #{offset}" unless bytes[offset, expected.bytesize] == expected.b
  offset + expected.bytesize
end

def gpu_component(source)
  # Mask comments and strings while preserving offsets for balanced braces.
  masked = source.gsub(%r{//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"}m) { |s| ' ' * s.bytesize }
  matches = masked.to_enum(:scan, /Device\s*\(GPU0\)\s*\{/).map { Regexp.last_match }
  raise 'expected one GPU0 device' unless matches.length == 1
  start = matches[0].begin(0)
  pos = matches[0].end(0)
  depth = 1
  while depth > 0 && pos < masked.bytesize
    depth += 1 if masked.getbyte(pos) == 123
    depth -= 1 if masked.getbyte(pos) == 125
    pos += 1
  end
  raise 'unbalanced GPU0 braces' unless depth.zero?
  source.byteslice(start, pos - start)
end

Dir.mktmpdir('bcm2712-display-asl-') do |work|
  asl_path = platform + '/Platform/RaspberryPi/RPi5/AcpiTables/Dsdt.asl'
  macros_path = platform + '/Platform/RaspberryPi/RPi5/AcpiTables/AcpiTables.h'
  hardware_path = platform + '/Silicon/Broadcom/Bcm27xx/Include/IndustryStandard/Bcm2712.h'
  body = gpu_component(File.binread(asl_path))
  lines = File.readlines(macros_path)
  macros = lines.each_index.filter_map do |index|
    next unless lines[index].match?(/^#define (?:QWORDMEMORY_BUF|QWORD_SET)\(/)
    selected = []
    loop do
      line = lines[index] or raise "truncated macro"
      selected << line
      index += 1
      break unless line.rstrip.getbyte(-1) == 92
    end
    selected.join
  end.join("\n")
  raise 'missing production resource macros' unless macros.scan(/^#define /).length == 2
  File.write(work + '/hardware.h', File.read(hardware_path))
  File.write(work + '/gpu.asl', "#include \"hardware.h\"\n" + macros + "\nDefinitionBlock (\"\", \"DSDT\", 2, \"EMBER\", \"DISPLAY\", 1) {\nScope (\\_SB_) {\n" + body + "\n}\n}\n")
  # ASL preprocessing uses a host compiler even when CC builds a target test.
  cpp = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  run(*cpp, '-E', '-P', '-x', 'c', '-I' + work, work + '/gpu.asl', '-o', work + '/gpu.pre.asl')
  run(*iasl, '-we', '-p', work + '/gpu', work + '/gpu.pre.asl')
  aml = File.binread(work + '/gpu.aml')
  raise 'invalid DSDT header' unless aml[0, 4] == 'DSDT' && aml[4, 4].unpack1('V') == aml.bytesize && aml.bytes.sum % 256 == 0
  name = aml.index('GPU0') or raise 'GPU0 missing from AML'
  pkg = (name - 6...name).filter_map do |at|
    next unless aml[at, 2] == "\x5b\x82".b
    length, count = package_length(aml, at + 2)
    [at + 2, length, count] if at + 2 + count == name
  end
  raise 'GPU0 package boundary' unless pkg.length == 1
  at, length, count = pkg[0]
  gpu = aml[name, length - count]
  dsd = gpu.index("\x08_DSD".b) or raise 'missing display _DSD'
  properties, = object(gpu, dsd + 5)
  uuid = ['14d8ffda ba6e8c4d8a91bc9bbf4aa301'.delete(' ')].pack('H*')
  raise 'unsupported display _DSD' unless properties == [{ buffer: uuid }, [['emberbsd,display-contract-version', 1]]]
  rbuf = gpu.index("\x08RBUF\x11".b) or raise 'missing resource buffer'
  buffer, pos = object(gpu, rbuf + 5)
  bytes = buffer.fetch(:buffer)
  memory = []
  irq = []
  cursor = 0
  while cursor < bytes.bytesize
    tag = bytes.getbyte(cursor)
    if tag == 0x79
      raise 'end tag boundary' unless cursor + 2 == bytes.bytesize
      cursor += 2
      next
    end
    size = bytes[cursor + 1, 2]&.unpack1('v')
    data = size && bytes[cursor + 3, size]
    raise 'resource descriptor boundary' unless data && data.bytesize == size
    if tag == 0x8a
      raise 'QWordMemory flags' unless size == 43 && data[0, 3] == [0, 13, 1].pack('C*')
      raise 'QWordMemory granularity' unless data[3, 8].unpack1('Q<').zero?
      memory << { offset: cursor }
    elsif tag == 0x89
      raise 'ExtendedIRQ flags' unless size == 6 && data[0, 2] == [1, 1].pack('C*')
      irq << data[2, 4].unpack1('V')
    else
      raise 'unexpected resource descriptor'
    end
    cursor += size + 3
  end
  raise 'resource count mismatch' unless memory.length == 32 && irq.length == 7
  memory.each_with_index do |entry, index|
    suffix = '%02d' % index
    { 'MI' => 14, 'MA' => 22, 'TR' => 30, 'LE' => 38 }.each do |prefix, delta|
      pos = consume(gpu, pos, "\x8fRBUF")
      offset, pos = constant(gpu, pos)
      raise 'CreateQWordField offset' unless offset == entry[:offset] + delta
      pos = consume(gpu, pos, prefix + suffix)
    end
    values = {}
    %w[LE MI TR].each do |prefix|
      pos = consume(gpu, pos, "\x70")
      value, pos = constant(gpu, pos)
      pos = consume(gpu, pos, prefix + suffix)
      values[prefix] = value
    end
    raise 'memory translation' unless values['TR'].zero?
    pos = consume(gpu, pos, "\x74\x72" + 'MI' + suffix + 'LE' + suffix + "\x00\x01" + 'MA' + suffix)
    entry[:base] = values['MI']
    entry[:length] = values['LE']
  end
  consume(gpu, pos, "\xa4RBUF")

  # Reuse the host compatibility prelude. Compile the actual OS decoder.
  fixture = File.read(os + '/ember/tools/bcm2712-display-contract.c')
  prelude = fixture.split('/* ACPICA service ABI.').first
  File.write(work + '/display.h', File.read(os + '/sys/arch/arm/broadcom/bcm2712_display.h'))
  core = File.readlines(os + '/sys/arch/arm/broadcom/bcm2712_display.c').reject { |s| s.start_with?('#include ', '__KERNEL_RCSID(') }.join
  File.write(work + '/core.h', core)
  properties = File.readlines(os + '/sys/arch/evbarm/rpi/vcprop.h').grep(/^#define\s+(?:VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE)\s/).join
  File.write(work + '/properties.h', properties)
  vectors = memory.map { |e| "{ UINT64_C(0x%x), UINT64_C(0x%x) }" % [e[:base], e[:length]] }.join(",\n")
  test = prelude + "\nstatic const struct bcm2712_display_memory memory[] = {\n#{vectors}\n};\nstatic const uint32_t irq[] = {#{irq.join(',')}};\nint main(void) { struct bcm2712_display_contract out; int error = bcm2712_display_decode(1, memory, 32, irq, 7, &out); if (error != 0) { fprintf(stderr, \"compiled firmware rejected by OS decoder: %d\\n\", error); return 1; } puts(\"Compiled GPU0 resource contract accepted by the actual OS decoder\"); return 0; }\n"
  File.write(work + '/test.c', test)
  run(*cc, '-std=c11', '-Wall', '-Wextra', '-Werror', *cflags, '-I' + work, work + '/test.c', '-o', work + '/test')
  FileUtils.cp(work + '/test', ENV['DISPLAY_OUTPUT']) if ENV['DISPLAY_OUTPUT']
  if ENV['DISPLAY_COMPILE_ONLY'] == '1'
    puts 'OS decoder test compiled only; no runtime PASS claimed'
  else
    run(*Shellwords.split(ENV.fetch('TEST_RUNNER', '')), work + '/test')
  end
  if ENV['DISPLAY_EVIDENCE']
    FileUtils.mkdir_p(ENV['DISPLAY_EVIDENCE'])
    %w[gpu.asl gpu.pre.asl gpu.aml test.c hardware.h core.h display.h properties.h].each { |f| FileUtils.cp(work + '/' + f, ENV['DISPLAY_EVIDENCE'] + '/' + f) }
    inputs = [asl_path, macros_path, hardware_path,
              os + '/sys/arch/arm/broadcom/bcm2712_display.c',
              os + '/sys/arch/arm/broadcom/bcm2712_display.h',
              os + '/sys/arch/evbarm/rpi/vcprop.h',
              os + '/ember/tools/bcm2712-display-contract.c']
    receipt = { version: 1, memory: memory, irq: irq, commands: COMMANDS,
                inputs: inputs.to_h { |f| [f, Digest::SHA256.file(f).hexdigest] },
                scope: 'GPU0 ASL component and actual OS decoder; no complete firmware build or device execution' }
    File.write(ENV['DISPLAY_EVIDENCE'] + '/resources.json', JSON.pretty_generate(receipt) + "\n")
  end
end
