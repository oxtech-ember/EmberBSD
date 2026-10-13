#!/usr/bin/env ruby
# Origin: EmberBSD - copied-file contracts for the factory recovery hook.
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'zlib'
tool = File.join(__dir__, 'a133-recovery-env.rb')
abort 'FAIL: recovery copy preparer is missing' unless File.file?(tool)
require tool

vars = {
  'bootcmd' => 'run setargs_mmc boot_normal',
  'boot_normal' => 'run ${hook};run boot_android',
  'boot_android' => 'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery' => 'sunxi_flash read 45000000 recovery;bootm 45000000',
  'opaque' => "\xff\xfe;keep=this".b, 'empty_value' => '', 'bootdelay' => '0'
}
encode = lambda do |entries, pad = 0, leading = true|
  body = leading ? "\0".b : ''.b
  body << entries.map { |key, value| key.b + '=' + value.b + "\0" }.join << "\0"
  body = body.ljust(131068, pad.chr)
  [Zlib.crc32(body)].pack('V') + body
end
checks = 0
[[0, true], [255, false]].each do |pad, leading|
  original = encode.call(vars, pad, leading)
  candidate = A133Recovery.prepare(original)
  before = A133Env.decode(original)[:entries].to_h
  after = A133Env.decode(candidate)[:entries].to_h
  changed = (before.keys | after.keys).select { |key| before[key] != after[key] }.sort
  abort 'FAIL: unrelated env changes' unless changed == %w[ember_recovery_once ember_restore_normal hook] &&
    after['boot_normal'] == 'run ${hook};run boot_android' &&
    after['bootcmd'] == 'run setargs_mmc boot_normal' && after['opaque'] == before['opaque'] &&
    after['ember_restore_normal'] == 'setenv hook; saveenv' &&
    after['hook'] == 'ember_restore_normal ember_recovery_once'
  restored = A133Env.patch(candidate, {'hook' => '', 'ember_recovery_once' => '', 'ember_restore_normal' => ''})
  abort 'FAIL: byte restoration failed' unless restored == original
  checks += 1
end
%w[hook ember_restore_normal ember_recovery_once].each do |name|
  ['', 'run something'].each do |value|
    begin
      A133Recovery.prepare(encode.call(vars.merge(name => value)))
      abort 'FAIL: existing hook overwritten'
    rescue A133Recovery::Invalid => e
      abort 'FAIL: wrong collision reason' unless e.message == 'recovery_hook_already_present'
    end
    checks += 1
  end
end
%w[bootcmd boot_normal boot_android boot_recovery].each do |name|
  begin
    A133Recovery.prepare(encode.call(vars.merge(name => 'run custom')))
    abort 'FAIL: foreign factory profile accepted'
  rescue A133Recovery::Invalid => e
    abort 'FAIL: wrong profile reason' unless e.message == 'unsupported_factory_environment'
  end
  checks += 1
end
original = encode.call(vars)
bad = original.dup; bad.setbyte(0, bad.getbyte(0) ^ 1)
begin
  A133Recovery.prepare(bad)
  abort 'FAIL: corrupt CRC accepted'
rescue A133Recovery::Invalid => e
  abort 'FAIL: wrong CRC reason' unless e.message == 'invalid_environment'
end
checks += 1
Dir.mktmpdir('a133-recovery-') do |dir|
  input = File.join(dir, 'original')
  output = File.join(dir, 'candidate')
  File.binwrite(input, original)
  out, err, status = Open3.capture3(RbConfig.ruby, tool, input, output)
  result = JSON.parse(out)
  abort 'FAIL: candidate publication' unless status.success? && err.empty? &&
    result['status'] == 'recovery_copy_prepared' && result['installation_ready'] == false &&
    result['device_writes_performed'] == 0 && File.stat(output).mode & 0777 == 0600 &&
    result['output_sha256'] == Digest::SHA256.file(output).hexdigest &&
    File.binread(input) == original && !File.exist?(output + '.partial') && !out.include?(dir)
  checks += 1
  [output, input].each do |existing|
    saved = File.binread(existing)
    out, err, status = Open3.capture3(RbConfig.ruby, tool, input, existing)
    abort 'FAIL: existing output replaced' unless !status.success? && err.empty? &&
      JSON.parse(out)['reason'] == 'output_path_unavailable' && File.binread(existing) == saved
    checks += 1
  end
  File.symlink(File.join(dir, 'absent'), File.join(dir, 'dangling'))
  out, err, status = Open3.capture3(RbConfig.ruby, tool, input, File.join(dir, 'dangling'))
  abort 'FAIL: dangling output symlink replaced' unless !status.success? && err.empty? &&
    JSON.parse(out)['reason'] == 'output_path_unavailable' && File.symlink?(File.join(dir, 'dangling'))
  checks += 1
  File.symlink(input, File.join(dir, 'input-link'))
  out, err, status = Open3.capture3(RbConfig.ruby, tool, File.join(dir, 'input-link'), File.join(dir, 'next'))
  abort 'FAIL: input symlink accepted' unless !status.success? && err.empty? &&
    JSON.parse(out)['reason'] == 'input_not_regular' && !File.exist?(File.join(dir, 'next'))
  checks += 1
  File.binwrite(input, '')
  out, err, status = Open3.capture3(RbConfig.ruby, tool, input, File.join(dir, 'next'))
  abort 'FAIL: empty env accepted' unless !status.success? && err.empty? &&
    JSON.parse(out)['reason'] == 'invalid_environment' && !File.exist?(File.join(dir, 'next'))
  checks += 1
end
puts "Recovery copy: #{checks} preservation, profile and publication cases passed"
