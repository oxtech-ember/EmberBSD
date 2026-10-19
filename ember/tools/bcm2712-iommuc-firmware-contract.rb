#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual ICAC/GPU0 components and reject malformed AML controls.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'rbconfig'

abort 'usage: bcm2712-iommuc-firmware-contract.rb PATCHED_PLATFORMS OS_SOURCE EMPTY_OUTPUT' unless ARGV.length == 3
platform, source = ARGV.first(2).map { |p| File.realpath(p) }
output = File.expand_path(ARGV[2])
abort 'Output must not be a symlink' if File.symlink?(output)
output = File.join(File.realpath(File.dirname(output)), File.basename(output))
abort 'Output must be empty' if File.exist?(output) &&
  (!File.directory?(output) || !Dir.empty?(output))
abort 'Output must not overlap inputs' if [platform, source].any? { |p|
  output == p || output.start_with?(p + '/') || p.start_with?(output + '/') }
FileUtils.mkdir_p(output)
commands = []
checker = source + '/ember/tools/bcm2712-display-firmware-contract.rb'
run = lambda do |name, input, required, expected = nil|
  env = { 'DISPLAY_EVIDENCE' => output + '/' + name, 'IOMMUC_REQUIRED' => required ? '1' : '0',
          'DISPLAY_COMPILED_AML' => input }
  argv = [RbConfig.ruby, checker, platform, source]
  out, err, status = Open3.capture3(env, *argv)
  File.write(output + '/' + name + '.stdout', out)
  File.write(output + '/' + name + '.stderr', err)
  commands << { name: name, argv: argv, environment: env, status: status.exitstatus }
  File.write(output + '/commands.json', JSON.pretty_generate(commands) + "\n")
  accepted = expected ? !status.success? && (out + err).include?(expected) : status.success?
  abort "Unexpected #{name} result; see saved output" unless accepted
end
run.call('positive', nil, true)
original = File.binread(output + '/positive/checked.aml')
locations = []
at = 0
while (at = original.index('ICAC'.b, at))
  locations << at
  at += 4
end
abort 'Expected declaration and dependency names' unless locations.length == 2
name, reference = locations
address = [0x1000005b00].pack('Q<')
abort 'Expected one cache address operand' unless original.scan(Regexp.new(Regexp.escape(address), Regexp::NOENCODING)).length == 1
address_at = original.index(address)
property = "emberbsd,iommuc-contract-version\0".b
version = original.index(property) or abort 'Missing cache version'
version += property.bytesize
abort 'Expected version OneOp' unless original.getbyte(version) == 1
flags = original.index("\x8a\x2b\x00".b, name) or abort 'Missing cache QWord'
abort 'Cache descriptor order' unless flags < reference && original.getbyte(flags + 5) == 1
controls = {
  'reference-string' => [reference, "\x0dXY\x00".b, 'GPU0 dependency is not an ICAC object reference'],
  'reference-other' => [reference, 'GPU0'.b, 'GPU0 dependency is not an ICAC object reference'],
  'missing-device' => [name, 'FAKE'.b, 'expected one ICAC device package'],
  'address' => [address_at, [0x1000005b01].pack('Q<'), 'unsupported ICAC geometry'],
  'version' => [version, "\x00".b, 'unsupported ICAC _DSD'],
  'cacheable' => [flags + 5, "\x03".b, 'QWordMemory flags']
}
controls.each do |control, (offset, replacement, expected)|
  aml = original.dup
  aml[offset, replacement.bytesize] = replacement
  aml.setbyte(9, 0)
  aml.setbyte(9, (-aml.bytes.sum) & 255)
  path = output + '/' + control + '.aml'
  File.binwrite(path, aml)
  run.call(control, path, true, expected)
end
profile = JSON.parse(File.read(output + '/positive/resources.json'))
if ENV['ICAC_BASELINE_PLATFORM']
  platform = File.realpath(ENV.fetch('ICAC_BASELINE_PLATFORM'))
  run.call('baseline', nil, false)
  previous = JSON.parse(File.read(output + '/baseline/resources.json'))
  %w[gpu_resource_program_sha256 gpu_dsd_sha256].each do |key|
    abort "GPU0 bytes changed: #{key}" unless previous.fetch(key) == profile.fetch(key)
  end
end
receipt = { controls: controls.keys, descriptor: profile.fetch('iommuc'),
            gpu_resource_program_sha256: profile.fetch('gpu_resource_program_sha256'),
            gpu_dsd_sha256: profile.fetch('gpu_dsd_sha256'),
            component_sha256: Digest::SHA256.hexdigest(original),
            scope: 'Actual compiled components and checked AML mutations only. No full image, live namespace, provider readiness, MMIO or power acceptance.' }
File.write(output + '/receipt.json', JSON.pretty_generate(receipt) + "\n")
puts 'ICAC compiled descriptor: positive and six AML causal controls PASS'
