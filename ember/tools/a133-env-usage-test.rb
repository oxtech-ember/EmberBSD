#!/usr/bin/env ruby
# Origin: EmberBSD - read-only Android env usage guard with task/alias/holder failures.
require 'tmpdir'
require_relative 'a133-usb-test-support'
require_relative 'a133-usb-backup-source'
checks=0
root="1 0 0:1 / / rw - rootfs rootfs rw\n"
mount=lambda { |id| root+"2 1 #{id} / /alias rw - ext4 /dev/block/alias rw\n" }
Dir.mktmpdir('a133-env-usage-') do |dir|
  UsbFixture.create(dir)
  File.write(File.join(dir,'layout.json'),JSON.generate(UsbFixture::PARTS))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  before=Digest::SHA256.file(File.join(dir,'part2')).hexdigest
  cases=[
    [{},nil],
    [{'mountinfo'=>mount.call('179:17'),'unrelated_holders'=>true},nil],
    [{'mountinfo'=>mount.call('179:2')},'usb_target_mounted'],
    [{'mountinfo'=>mount.call('179:0')},'usb_target_mounted'],
    [{'mountinfo'=>mount.call('179:17'),'thread_mountinfo'=>mount.call('179:2')},'usb_target_mounted'],
    [{'mountinfo'=>mount.call('253:0'),'holders'=>true},'usb_target_in_use'],
    [{'holders'=>true},'usb_target_in_use'],
    [{'inventory_unreadable'=>true},'usb_command_failed'],
    [{'thread_unreadable'=>true},'usb_command_failed'],
    [{'mountinfo'=>''},'usb_mount_inventory_invalid'],
    [{'mountinfo'=>"2 1 private / / rw - ext4 alias rw\n"},'usb_mount_inventory_invalid'],
    [{'dev_ids'=>"179:0\n179:0\n"},'usb_environment_device_ids_invalid'],
    [{'dev_ids'=>"179:0\n"},'usb_environment_device_ids_invalid'],
    [{'dev_ids'=>'bad'},'usb_environment_device_ids_invalid'],
    [{'usage_unreadable'=>true},'usb_command_failed'],
    [{'holders_unreadable'=>true},'usb_command_failed'],
    [{'swaps'=>"Filename Type Size Used Priority\n/dev/block/zram0 partition 1 0 -2\n"},'usb_target_in_use'],
    [{'swaps'=>''},'usb_swap_inventory_invalid']
  ]
  cases.each do |state,reason|
    UsbFixture.state(dir,{'state'=>'device'}.merge(state))
    source=A133UsbBackup::Source.new(adb:File.join(dir,'adb'),serial:'PROTECTION_TEST_SERIAL',state:'device',root_method:'vendor_su',timeout:30)
    begin
      result=source.environment_inspect!
      abort 'FAIL: unsafe env usage accepted '+reason if reason
      abort 'FAIL: profile mismatch' unless result[:cid]==UsbFixture::CID && result[:state]=='device'
    rescue A133UsbBackup::Invalid => error
      abort "FAIL: usage expected #{reason}, got #{error.message}" unless error.message==reason && error.cause.nil?
    end
    abort 'FAIL: read-only guard wrote env' unless Digest::SHA256.file(File.join(dir,'part2')).hexdigest==before && !File.exist?(File.join(dir,'written'))
    checks+=1
  end
  UsbFixture.state(dir,{'state'=>'recovery','mountinfo'=>mount.call('179:17'),'block_ids'=>['179:17']})
  begin
    A133UsbBackup::Source.new(adb:File.join(dir,'adb'),serial:'PROTECTION_TEST_SERIAL',state:'recovery',root_method:'adbd',timeout:30).environment_inspect!
    abort 'FAIL: recovery allowed unrelated block mount'
  rescue A133UsbBackup::Invalid => error
    abort 'FAIL: recovery lost stricter guard' unless error.message=='usb_target_mounted'
  end
  checks+=1
end
puts "Env usage: #{checks} Android/recovery read-only cases passed"
