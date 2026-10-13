#!/usr/bin/env ruby
# Origin: EmberBSD - regression for mounts hidden in nonleader recovery thread namespaces.
require 'tmpdir'
require_relative 'a133-usb-mutable-backup'
require_relative 'a133-mutable-test-support'
MutableFixture.configure
failures=[]; checks=0
Dir.mktmpdir('mutable-thread-') do |parent|
  File.chmod(0700,parent); source=File.join(parent,'source'); MutableFixture.write(source)
  adb=File.join(source,'adb'); File.write(adb,File.read(File.join(__dir__,'a133-usb-mutable-fixture.rb'))); File.chmod(0700,adb)
  [
    ['thread_mount','usb_target_mounted',{'thread_mountinfo'=>"2 1 179:6 / /thread-data rw - ext4 /dev/block/alias rw\n",'block_ids'=>['179:6']}],
    ['thread_unreadable','usb_command_failed',{'thread_unreadable'=>true}]
  ].each do |name,reason,state|
    File.write(File.join(source,'state.json'),JSON.generate(state)); directory=File.join(parent,name)
    begin
      A133UsbBackup.collect_mutable(directory:directory,serial:'MUTABLE_TEST_SERIAL',cid:'0123456789abcdef0123456789abcdef',adb:adb,timeout:30)
      failures << name+': accepted incomplete thread inventory'
    rescue A133UsbBackup::Invalid => error
      if error.message==reason && error.cause.nil? && !error.full_message.include?('PRIVATE_THREAD_DIAGNOSTIC')
        checks+=1
      else
        failures << name+': unexpected '+error.message
      end
    end
    failures << name+': created payload/manifest before rejecting initial inventory' if File.exist?(directory)
  end
end
abort failures.map { |value| 'FAIL: '+value }.join("\n") unless failures.empty?
puts "USB mutable thread inventory: #{checks} nonleader mount and unreadable inventory cases passed"
