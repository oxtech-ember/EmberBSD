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

def device_aml(aml, device_name)
  raise 'invalid DSDT header' unless aml[0, 4] == 'DSDT' &&
    aml[4, 4]&.unpack1('V') == aml.bytesize && aml.bytes.sum % 256 == 0
  # A full table can reference GPU0 before it declares the device.
  packages = aml.to_enum(:scan, Regexp.new(Regexp.escape(device_name), Regexp::NOENCODING)).flat_map do
    name = Regexp.last_match.begin(0)
    (name - 6...name).filter_map do |at|
      next unless aml[at, 2] == "\x5b\x82".b
      length, count = package_length(aml, at + 2)
      aml[name, length - count] if at + 2 + count == name
    end
  end
  raise "expected one #{device_name} device package" unless packages.length == 1
  packages.first
end

def device_component(source, device_name, optional = false)
  # Mask comments and strings while preserving offsets for balanced braces.
  masked = source.gsub(%r{//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"}m) { |s| ' ' * s.bytesize }
  matches = masked.to_enum(:scan, /Device\s*\(#{Regexp.escape(device_name)}\)\s*\{/).map { Regexp.last_match }
  return nil if optional && matches.empty?
  raise "expected one #{device_name} device" unless matches.length == 1
  start = matches[0].begin(0)
  pos = matches[0].end(0)
  depth = 1
  while depth > 0 && pos < masked.bytesize
    depth += 1 if masked.getbyte(pos) == 123
    depth -= 1 if masked.getbyte(pos) == 125
    pos += 1
  end
  raise "unbalanced #{device_name} braces" unless depth.zero?
  source.byteslice(start, pos - start)
end

def resource_program(device, nmemory, nirq)
  rbuf = device.index("\x08RBUF\x11".b) or raise 'missing resource buffer'
  buffer, pos = object(device, rbuf + 5)
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
  raise 'resource count mismatch' unless memory.length == nmemory && irq.length == nirq
  memory.each_with_index do |entry, index|
    suffix = '%02d' % index
    { 'MI' => 14, 'MA' => 22, 'TR' => 30, 'LE' => 38 }.each do |prefix, delta|
      pos = consume(device, pos, "\x8fRBUF")
      offset, pos = constant(device, pos)
      raise 'CreateQWordField offset' unless offset == entry[:offset] + delta
      pos = consume(device, pos, prefix + suffix)
    end
    values = {}
    %w[LE MI TR].each do |prefix|
      pos = consume(device, pos, "\x70")
      value, pos = constant(device, pos)
      pos = consume(device, pos, prefix + suffix)
      values[prefix] = value
    end
    raise 'memory translation' unless values['TR'].zero?
    pos = consume(device, pos, "\x74\x72" + 'MI' + suffix + 'LE' + suffix + "\x00\x01" + 'MA' + suffix)
    entry[:base] = values['MI']
    entry[:length] = values['LE']
  end
  finish = consume(device, pos, "\xa4RBUF")
  [memory, irq, device[rbuf...finish]]

end

Dir.mktmpdir('bcm2712-display-asl-') do |work|
  asl_path = platform + '/Platform/RaspberryPi/RPi5/AcpiTables/Dsdt.asl'
  macros_path = platform + '/Platform/RaspberryPi/RPi5/AcpiTables/AcpiTables.h'
  hardware_path = platform + '/Silicon/Broadcom/Bcm27xx/Include/IndustryStandard/Bcm2712.h'
  source = File.binread(asl_path)
  gpu_body = device_component(source, 'GPU0')
  cache_body = device_component(source, 'ICAC', true)
  raise 'required ICAC device is absent' if ENV['IOMMUC_REQUIRED'] == '1' && !cache_body
  body = [cache_body, gpu_body].compact.join("\n")
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
  compiled_input = ENV['DISPLAY_COMPILED_AML']
  aml_path = compiled_input ? File.realpath(compiled_input) : work + '/gpu.aml'
  raise 'compiled AML input must be a regular file' unless File.file?(aml_path)
  aml = File.binread(aml_path)
  gpu = device_aml(aml, 'GPU0')
  dsd = gpu.index("\x08_DSD".b) or raise 'missing display _DSD'
  properties, dsd_finish = object(gpu, dsd + 5)
  uuid = ['14d8ffda ba6e8c4d8a91bc9bbf4aa301'.delete(' ')].pack('H*')
  raise 'unsupported display _DSD' unless properties == [{ buffer: uuid }, [['emberbsd,display-contract-version', 1]]]
  memory, irq, resource_bytes = resource_program(gpu, 32, 7)
  cache_contract = nil
  if cache_body
    cache = device_aml(aml, 'ICAC')
    { '_HID' => 'PRP0001', '_UID' => 0, '_STA' => 15 }.each do |name, expected|
      at = cache.index("\x08".b + name) or raise "missing ICAC #{name}"
      actual, = object(cache, at + 5)
      raise "unsupported ICAC #{name}" unless actual == expected
    end
    at = cache.index("\x08_DSD".b) or raise 'missing ICAC _DSD'
    profile, = object(cache, at + 5)
    raise 'unsupported ICAC _DSD' unless profile == [{ buffer: uuid },
      [['compatible', 'brcm,bcm2712-iommuc'], ['emberbsd,iommuc-contract-version', 1]]]
    cache_memory, cache_irq, = resource_program(cache, 1, 0)
    raise 'unsupported ICAC geometry' unless cache_memory.first.values_at(:base, :length) == [0x1000005b00, 0x80]
    raise 'unexpected ICAC IRQ' unless cache_irq.empty?
    at = gpu.index("\x08_DEP\x12".b) or raise 'missing GPU0 reference package'
    length, count = package_length(gpu, at + 6)
    pos = at + 6 + count
    raise 'expected one firmware ICAC dependency' unless gpu.getbyte(pos) == 1
    reference = gpu[pos + 1...at + 6 + length]
    # Canonical full reference, or its compiler-reduced sibling name.
    valid = ["\x5c\x2e_SB_ICAC".b, "\x5eICAC".b, 'ICAC'.b]
    raise 'GPU0 dependency is not an ICAC object reference' unless valid.include?(reference)
    cache_contract = { version: 1, memory: cache_memory, dependency: reference.unpack1('H*'),
                       scope: 'Descriptor/reference only. No provider readiness, power/access or cache operation.' }
  end


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
    File.binwrite(ENV['DISPLAY_EVIDENCE'] + '/checked.aml', aml)
    inputs = [asl_path, macros_path, hardware_path,
              os + '/sys/arch/arm/broadcom/bcm2712_display.c',
              os + '/sys/arch/arm/broadcom/bcm2712_display.h',
              os + '/sys/arch/evbarm/rpi/vcprop.h',
              os + '/ember/tools/bcm2712-display-contract.c']
    inputs << aml_path if compiled_input
    receipt = { version: 1, memory: memory, irq: irq, commands: COMMANDS,
                gpu_resource_program_sha256: Digest::SHA256.hexdigest(resource_bytes),
                gpu_dsd_sha256: Digest::SHA256.hexdigest(gpu[dsd...dsd_finish]),
                iommuc: cache_contract,
                aml: { kind: compiled_input ? 'compiled-dsdt' : 'component',
                       sha256: Digest::SHA256.hexdigest(aml), bytes: aml.bytesize },
                inputs: inputs.to_h { |f| [f, Digest::SHA256.file(f).hexdigest] },
                scope: 'GPU0 ASL and actual OS decoder, optional ICAC descriptor/reference; optional compiled DSDT input. No image packaging, live namespace, or device execution check.' }
    File.write(ENV['DISPLAY_EVIDENCE'] + '/resources.json', JSON.pretty_generate(receipt) + "\n")
  end
end
