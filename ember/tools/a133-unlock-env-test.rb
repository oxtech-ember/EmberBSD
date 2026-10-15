#!/usr/bin/env ruby
# Origin: EmberBSD - independent copied-file contracts for vendor unlock scheduling.
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'zlib'

tool = File.join(__dir__, 'a133-unlock-env.rb')
abort 'FAIL: unlock copy preparer is missing' unless File.file?(tool)
require tool

# Replacing the unlock/reset ordering, changing normal boot, overwriting a
# reserved helper or publishing an unsafe path must fail these contracts.
vars = {
  'bootcmd' => 'run setargs_mmc boot_normal',
  'boot_normal' => 'run ${hook};run boot_android',
  'boot_android' => 'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery' => 'sunxi_flash read 45000000 recovery;bootm 45000000',
  'opaque' => "\xff\xfe;keep=this".b, 'empty_value' => '', 'bootdelay' => '0'
}
# Independently recorded vendor sequence: set the secure-storage flag, save a
# one-shot recovery hook, then reset so the factory loader refreshes the flag.
updates = {
  'hook' => 'ember_unlock_flag ember_unlock_next',
  'ember_unlock_flag' => 'pst write fastboot_status_flag unlocked',
  'ember_unlock_next' => 'setenv hook ember_restore_normal ember_recovery_once; run ember_save_env ember_reset',
  'ember_save_env' => 'saveenv',
  'ember_reset' => 'reset',
  'ember_restore_normal' => 'setenv hook; saveenv',
  'ember_recovery_once' => 'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'
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
  saved = original.dup
  candidate = A133Unlock.prepare(original)
  expected = encode.call(vars.merge(updates), pad, leading)
  abort 'FAIL: unlock sequence or unrelated bytes changed' unless candidate == expected && original == saved
  restored = A133Env.patch(candidate, updates.transform_values { '' })
  abort 'FAIL: original byte restoration failed' unless restored == original
  checks += 1
end
updates.each_key do |name|
  ['', 'run existing'].each do |value|
    begin
      A133Unlock.prepare(encode.call(vars.merge(name => value)))
      abort 'FAIL: reserved hook overwritten'
    rescue A133Unlock::Invalid => error
      abort 'FAIL: wrong collision reason' unless error.message == 'unlock_hook_already_present'
    end
    checks += 1
  end
end
%w[bootcmd boot_normal boot_android boot_recovery].each do |name|
  [vars.reject { |key, _| key == name }, vars.merge(name => 'run custom')].each do |profile|
    begin
      A133Unlock.prepare(encode.call(profile))
      abort 'FAIL: foreign factory profile accepted'
    rescue A133Unlock::Invalid => error
      abort 'FAIL: wrong profile reason' unless error.message == 'unsupported_factory_environment'
    end
    checks += 1
  end
end
original = encode.call(vars)
bad_crc = original.dup
bad_crc.setbyte(0, bad_crc.getbyte(0) ^ 1)
bad_terminator = [Zlib.crc32('x' * 131068)].pack('V') + 'x' * 131068
duplicate = encode.call(vars.to_a + [['bootdelay', '3']])
overfull = encode.call(vars.merge('large' => 'x' * (131068 - original.index("\0\0", 5) - 12)))
[nil, 7, '', original.byteslice(0, 131071), original + 'x', bad_crc, bad_terminator, duplicate, overfull].each do |input|
  begin
    A133Unlock.prepare(input)
    abort 'FAIL: malformed or overflowing env accepted'
  rescue A133Unlock::Invalid => error
    abort 'FAIL: wrong env failure' unless error.message == 'invalid_environment'
  end
  checks += 1
end

Dir.mktmpdir('a133-unlock-') do |dir|
  input = File.join(dir, 'original')
  output = File.join(dir, 'candidate')
  File.binwrite(input, original)
  run = lambda do |*args|
    out, err, status = Open3.capture3(RbConfig.ruby, tool, *args)
    abort 'FAIL: diagnostics expose host paths' unless err.empty? && !out.include?(dir)
    [JSON.parse(out), status]
  end
  result, status = run.call(input, output)
  abort 'FAIL: private candidate publication' unless status.success? &&
    result['status'] == 'unlock_copy_prepared' && result['installation_ready'] == false &&
    result['device_writes_performed'] == 0 && result['host_files_created'] == 1 &&
    result['input_sha256'] == Digest::SHA256.hexdigest(original) &&
    result['output_sha256'] == Digest::SHA256.file(output).hexdigest &&
    result['changed_variables'] == updates.keys.sort &&
    File.binread(output) == encode.call(vars.merge(updates)) &&
    File.stat(output).mode & 0777 == 0600 && !File.exist?(output + '.partial') &&
    File.binread(input) == original
  checks += 1

  [input, output].each do |existing|
    saved = File.binread(existing)
    result, status = run.call(input, existing)
    abort 'FAIL: existing output replaced' unless !status.success? &&
      result['reason'] == 'output_path_unavailable' && File.binread(existing) == saved
    checks += 1
  end
  next_output = File.join(dir, 'next')
  partial = next_output + '.partial'
  File.binwrite(partial, 'FOREIGN_PARTIAL')
  result, status = run.call(input, next_output)
  abort 'FAIL: foreign partial overwritten or removed' unless !status.success? &&
    result['reason'] == 'output_path_unavailable' && File.binread(partial) == 'FOREIGN_PARTIAL' &&
    !File.exist?(next_output)
  File.unlink(partial)
  checks += 1
  File.symlink(File.join(dir, 'absent'), next_output)
  result, status = run.call(input, next_output)
  abort 'FAIL: dangling output replaced' unless !status.success? &&
    result['reason'] == 'output_path_unavailable' && File.symlink?(next_output)
  File.unlink(next_output)
  checks += 1
  link = File.join(dir, 'input-link')
  File.symlink(input, link)
  result, status = run.call(link, next_output)
  abort 'FAIL: input symlink followed' unless !status.success? &&
    result['reason'] == 'input_not_regular' && !File.exist?(next_output)
  checks += 1
  File.mkfifo(File.join(dir, 'input-fifo'), 0600)
  result, status = run.call(File.join(dir, 'input-fifo'), next_output)
  abort 'FAIL: FIFO input accepted' unless !status.success? &&
    result['reason'] == 'input_not_regular' && !File.exist?(next_output)
  checks += 1
  [[], [input], [input, next_output, 'extra']].each do |args|
    result, status = run.call(*args)
    abort 'FAIL: bad argument count accepted' unless !status.success? && result['reason'] == 'invalid_arguments'
    checks += 1
  end
  File.binwrite(input, '')
  result, status = run.call(input, next_output)
  abort 'FAIL: empty input published' unless !status.success? &&
    result['reason'] == 'invalid_environment' && !File.exist?(next_output) && !File.exist?(partial)
  checks += 1
end
puts "Unlock copy: #{checks} byte-preservation, refusal and publication cases passed"
