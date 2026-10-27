#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
# Independent direct-profile bytes. This test performs no device operations.
require 'zlib'
require_relative 'a133-recovery-env'
require_relative 'a133-usb-transfer'

def encode(entries, pad, leading)
  body = leading ? "\0".b : ''.b
  entries.each { |key, value| body << key.b + '=' + value.b + "\0" }
  body << "\0"
  raise 'fixture overflow' if body.bytesize > 131068
  body = body.ljust(131068, pad.chr)
  [Zlib.crc32(body)].pack('V') + body
end

factory = {
  'bootcmd' => 'run setargs_nand boot_normal',
  'boot_normal' => 'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery' => 'sunxi_flash read 45000000 recovery;bootm 45000000',
  'setargs_nand' => 'setenv bootargs earlyprintk=${earlyprintk} initcall_debug=${initcall_debug} ' \
    'console=${console} loglevel=${loglevel} root=${nand_root} init=${init} partitions=${partitions} ' \
    'cma=${cma} snum=${snum} mac_addr=${mac} wifi_mac=${wifi_mac} bt_mac=${bt_mac} ' \
    'specialstr=${specialstr} gpt=1',
  'opaque' => "\xff\xfe;keep=this".b,
  'empty_value' => '', 'bootdelay' => '0'
}
recovery = {
  'ember_entry_fdt' => 'fdt addr ${fdtcontroladdr}',
  'ember_entry_usb' => 'fdt set /soc@03000000/usbc0@0 usb_port_type <0>',
  'ember_recovery_once' => 'run ember_entry_fdt ember_entry_usb boot_recovery'
}
trial_helpers = {
  'ember_trial_rearm' => 'setenv bootcmd run setargs_nand ember_recovery_once',
  'ember_trial_save' => 'saveenv',
  'ember_trial_load' => 'sunxi_flash read 45000000 boot',
  'ember_trial_boot' => 'bootm 45000000'
}
checks = 0
[0, 255].product([true, false]).each do |pad, leading|
  original = encode(factory, pad, leading)
  before = original.dup
  protected_vars = factory.merge('bootcmd' => 'run setargs_nand ember_recovery_once').merge(recovery)
  protected = A133Recovery.protect(original)
  abort 'FAIL: direct protection bytes or caller input' unless
    protected == encode(protected_vars, pad, leading) && original == before
  expected_rearmed = protected_vars.merge(trial_helpers)
  expected_trial = expected_rearmed.merge('bootcmd' =>
    'run ember_trial_rearm ember_trial_save setargs_nand ember_trial_load ember_trial_boot')
  result = A133Recovery.trial(original)
  abort 'FAIL: direct trial/rearmed bytes' unless result == {
    trial_env: encode(expected_trial, pad, leading),
    rearmed_env: encode(expected_rearmed, pad, leading), installation_ready: false
  }
  abort 'FAIL: mutable trial plan' unless result.frozen? &&
    result[:trial_env].frozen? && result[:rearmed_env].frozen?
  reverse = (recovery.keys + trial_helpers.keys).to_h { |key| [key, ''] }
  reverse['bootcmd'] = factory.fetch('bootcmd')
  abort 'FAIL: direct trial reversal' unless A133Env.patch(result[:trial_env], reverse) == before
  abort 'FAIL: direct rearmed reversal' unless A133Env.patch(result[:rearmed_env], reverse) == before
  client = A133Usb::Client.new(adb: 'adb', serial: 'TEST_SERIAL', cid: '0' * 32)
  backup = {'status' => 'backup_integrity_verified', 'cid' => '0' * 32,
    'bytes' => 31037849600, 'gpt_headers_crc' => true, 'gpt_arrays_crc' => true,
    'partition_layout_verified' => true, 'uncompressed_sha256' => 'a' * 64,
    'partition_sha256' => %w[bootloader env boot recovery].to_h { |role| [role, 'b' * 64] }}
  begin
    client.write_verified(role: 'boot', path: File.join(__dir__, 'absent-direct-test-image'), bytes: 33554432,
      sha256: 'f' * 64, backup: backup, protected_env: protected)
    abort 'FAIL: unavailable image accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: direct protection rejected before image check' unless error.message == 'image_unavailable'
  end
  %w[bootcmd boot_normal boot_recovery setargs_nand ember_entry_fdt ember_entry_usb ember_recovery_once].each do |key|
    invalid = encode(protected_vars.merge(key => ''), pad, leading)
    begin
      client.write_verified(role: 'boot', path: File.join(__dir__, 'absent-direct-test-image'), bytes: 33554432,
        sha256: 'f' * 64, backup: backup, protected_env: invalid)
      abort 'FAIL: damaged direct protection accepted'
    rescue A133Usb::Invalid => error
      abort 'FAIL: damaged protection reached image/device check' unless error.message == 'invalid_protected_environment'
    end
    checks += 1
  end
  ['run boot_normal', 'setenv bootargs console=ttyS0;run boot_normal'].each do |value|
    invalid = encode(protected_vars.merge('setargs_nand' => value), pad, leading)
    begin
      client.write_verified(role: 'boot', path: File.join(__dir__, 'absent-direct-test-image'), bytes: 33554432,
        sha256: 'f' * 64, backup: backup, protected_env: invalid)
      abort 'FAIL: executable setargs substitution accepted'
    rescue A133Usb::Invalid => error
      abort 'FAIL: executable setargs reached image/device check' unless error.message == 'invalid_protected_environment'
    end
    checks += 1
  end
  original.replace('changed caller input')
  abort 'FAIL: caller owns trial bytes' unless result[:trial_env] == encode(expected_trial, pad, leading)
  checks += 1
end

def reject(data, operation, reason)
  A133Recovery.public_send(operation, data)
  abort 'FAIL: unsafe direct input accepted'
rescue A133Recovery::Invalid => error
  abort 'FAIL: unexpected direct refusal ' + error.message unless error.message == reason
end

%w[bootcmd boot_normal boot_recovery setargs_nand].each do |name|
  reject(encode(factory.merge(name => ''), 0, true), :protect, 'unsupported_factory_environment')
  checks += 1
end
['run boot_normal', 'setenv bootargs console=ttyS0;run boot_normal'].each do |value|
  [:protect, :trial].each do |operation|
    reject(encode(factory.merge('setargs_nand' => value), 0, true), operation, 'unsupported_factory_environment')
    checks += 1
  end
end
%w[hook ember_restore_normal ember_recovery_once ember_entry_restore ember_entry_save ember_entry_fdt ember_entry_usb].each do |name|
  ['', 'run foreign'].each do |value|
    reject(encode(factory.merge(name => value), 0, true), :protect, 'recovery_hook_already_present')
    checks += 1
  end
end
trial_helpers.each_key do |name|
  ['', 'run foreign'].each do |value|
    reject(encode(factory.merge(name => value), 0, true), :trial, 'trial_hook_already_present')
    checks += 1
  end
end
puts "Direct protection/trial: #{checks} byte, writer-guard and refusal cases passed"
