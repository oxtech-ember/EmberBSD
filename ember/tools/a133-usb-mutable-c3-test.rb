#!/usr/bin/env ruby
# Origin: EmberBSD - exact C3 admission for read-only recovery copies.
require 'tmpdir'
require_relative 'a133-mutable-test-support'
require_relative 'a133-usb-mutable-backup'
MutableFixture.configure
checks=0
Dir.mktmpdir('mutable-c3-') do |parent|
  File.chmod(0700,parent)
  fixture=File.join(parent,'fixture');MutableFixture.write(fixture)
  adb=File.join(fixture,'adb')
  File.write(adb,File.read(File.join(__dir__,'a133-usb-mutable-fixture.rb')));File.chmod(0700,adb)
  [
    [{'model'=>'QUAD-CORE A133 c3'},nil],
    [{'model'=>'QUAD-CORE A133 c3 other'},'usb_board_mismatch'],
    [{'model'=>'QUAD-CORE A133 c3','thread_mountinfo'=>"1 0 179:6 / /alias rw - ext4 alias rw\n",'block_ids'=>['179:6']},'usb_target_mounted'],
    [{'model'=>'QUAD-CORE A133 c3','drift'=>true},'usb_backup_source_changed']
  ].each_with_index do |(state,reason),i|
    File.write(File.join(fixture,'state.json'),JSON.generate(state))
    destination=File.join(parent,"copy-#{i}")
    begin
      result=A133UsbBackup.collect_mutable(directory:destination,serial:'MUTABLE_TEST_SERIAL',cid:'0123456789abcdef0123456789abcdef',adb:adb,timeout:30)
      abort 'FAIL: unsafe C3 capture accepted' if reason
      abort 'FAIL: missing read-only C3 copies' unless result[:status]=='usb_mutable_captured' && result[:writes_performed]==0 && result[:mutable_copies_verified]==true
    rescue A133UsbBackup::Invalid => e
      abort "FAIL: expected #{reason}, got #{e.message}" unless e.message==reason
    end
    checks+=1
  end
end
puts "C3 mutable capture: #{checks} model, hidden-mount and identity cases passed"
