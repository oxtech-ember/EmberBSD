#!/usr/bin/env ruby
# Origin: EmberBSD - malformed launcher arguments fail before USB with conservative flags.
require 'tmpdir'
require_relative 'a133-recovery-reboot-test-support'
require_relative 'a133-recovery-readback'
require_relative 'a133-recovery-reboot'
checks=0
Dir.mktmpdir('a133-readback-argument-') do |dir|
  context=RebootFixture.create(dir)
  policy=A133Recovery::Policy.new(serial:RebootFixture::SERIAL,cid:UsbFixture::CID,
    original_env:context[:original],backup:context[:backup])
  inputs=["PRIVATE_LAUNCHER\0PATH",'PRIVATE_LAUNCHER'.encode('UTF-16LE'),"PRIVATE_LAUNCHER\xff".b.force_encoding('UTF-8')]
  inputs=inputs[1,1] if ARGV==['--wide']
  inputs.each do |adb|
    begin
      A133Recovery::Readback.new(adb:adb,policy:policy,state:'device',root_method:'vendor_su').verify(expected:'original')
      abort 'FAIL: malformed launcher accepted'
    rescue A133Usb::Invalid => error
      abort 'FAIL: launcher argument reason/flag/cause' unless error.message=='invalid_usb_options' && error.write_attempted==false &&
        error.cause.nil? && !error.full_message.include?('PRIVATE_LAUNCHER')
    end
    abort 'FAIL: malformed launcher reached USB' if File.exist?(File.join(dir,'observed')) || File.exist?(File.join(dir,'written'))
    checks+=1
    begin
      A133Recovery::Reboot.new(adb:adb,serial:RebootFixture::SERIAL,cid:UsbFixture::CID)
      abort 'FAIL: malformed reboot launcher accepted'
    rescue A133Recovery::Reboot::Invalid => error
      abort 'FAIL: reboot launcher argument reason/flags/cause' unless error.message=='invalid_reboot_options' &&
        error.reboot_attempted==false && error.write_attempted==false && error.cause.nil? && !error.full_message.include?('PRIVATE_LAUNCHER')
    end
    abort 'FAIL: malformed reboot launcher reached USB' if File.exist?(File.join(dir,'observed')) || File.exist?(File.join(dir,'written'))
    checks+=1
  end
  RebootFixture.reset(dir,context,{'state'=>'device'})
  adb=File.join(dir,"adb-caf\u00e9")
  File.write(adb,File.read(File.join(dir,'adb'))); File.chmod(0700,adb)
  A133Recovery::Reboot.new(adb:adb,serial:RebootFixture::SERIAL,cid:UsbFixture::CID)
  receipt=A133Recovery::Readback.new(adb:adb,policy:policy,state:'device',root_method:'vendor_su').verify(expected:'original')
  abort 'FAIL: valid non-ASCII UTF-8 launcher failed or wrote' unless receipt[:status]=='environment_state_verified' &&
    receipt[:writes_performed]==0 && !File.exist?(File.join(dir,'written'))
  checks+=1
end
puts "Recovery readback arguments: #{checks} malformed/valid UTF-8 launcher cases passed"
