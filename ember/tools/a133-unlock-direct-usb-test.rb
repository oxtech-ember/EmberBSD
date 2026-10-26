#!/usr/bin/env ruby
# Origin: EmberBSD - direct-factory unlock through actual-dd ADB boundary processes.
require 'tmpdir'
require_relative 'a133-recovery-reboot-test-support'
require_relative 'a133-unlock-stage'
checks=0
Dir.mktmpdir('a133-direct-unlock-usb-') do |dir|
  context=RebootFixture.create(dir,direct:true)
  tail=context[:original].byteslice(131072,16777216-131072)
  vars=A133Env.decode(context[:original].byteslice(0,131072)).fetch(:entries).to_h.merge(
    'mac'=>'','wifi_mac'=>'','bt_mac'=>'','specialstr'=>'')
  original=RebootFixture.encode(vars)+tail
  context[:original]=original
  context[:backup]['partition_sha256']['env']=Digest::SHA256.hexdigest(original)
  helpers={'ember_entry_restore'=>'setenv bootcmd run setargs_nand boot_normal',
    'ember_entry_save'=>'saveenv','ember_entry_fdt'=>'fdt addr ${fdtcontroladdr}',
    'ember_entry_usb'=>'fdt set /soc@03000000/usbc0@0 usb_port_type <0>',
    'ember_unlock_flag'=>'pst write fastboot_status_flag unlocked',
    'ember_unlock_next'=>'setenv bootcmd run ember_entry_restore ember_entry_save setargs_nand ember_entry_fdt ember_entry_usb boot_recovery; run ember_entry_save ember_reset',
    'ember_reset'=>'reset'}
  consumed=vars.merge(helpers)
  runtime=consumed.reject { |key,_| %w[wifi_mac bt_mac specialstr].include?(key) }.merge(
    'mac'=>'02:12:34:56:78:90','snum'=>'0011223344556677889','bootreason'=>'unknow',
    'fdtcontroladdr'=>'7beb3280','fileaddr'=>'40000000','filesize'=>'2a338','rotpk_status'=>'0',
    'partitions'=>UsbFixture::PARTS.each_with_index.map { |p,i| "#{p[0]}@mmcblk0p#{i+1}" }.join(':'))
  File.binwrite(File.join(dir,'saved-original'),original)
  File.write(File.join(dir,'adb-source.rb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-unlock-stage-fixture.rb')))
  mutable={'status'=>'mutable_integrity_verified','serial'=>RebootFixture::SERIAL,'cid'=>UsbFixture::CID,
    'mutable_copies_verified'=>true,'gpt_sha256'=>context[:backup]['gpt_sha256'],
    'observation'=>'recovery_unmounted_two_matching_reads','filesystem_consistency'=>'not_established_by_integrity_check',
    'partition_sha256'=>{'UDISK'=>'a'*64,'metadata'=>'b'*64}}
  count=lambda { |name| File.exist?(File.join(dir,name)) ? File.readlines(File.join(dir,name)).size : 0 }
  cases=[['plain',consumed,nil],['runtime',runtime,nil],
    ['changed-helper',runtime.merge('ember_entry_usb'=>'run foreign'),'usb_unlock_environment_unknown'],
    ['changed-opaque',runtime.merge('opaque'=>'foreign'),'usb_unlock_environment_unknown']]
  cases.each_with_index do |(label,returned,reason),index|
    session=File.join(dir,"session-#{index}")
    RebootFixture.reset(dir,context,{'state'=>'device','journal_path'=>File.join(session,'state.json')})
    File.binwrite(File.join(dir,'unlock-consumed.bin'),RebootFixture.encode(returned.to_a.reverse.to_h,true,"\0"))
    %w[write-count reboot-count].each { |name| File.unlink(File.join(dir,name)) if File.exist?(File.join(dir,name)) }
    options={directory:session,adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,
      original_env:original,backup:context[:backup],mutable:mutable,locked_round_trip_verified:true,
      device_root_method:'adbd',timeout:5,wait_timeout:1}
    begin
      result=A133Unlock::Stage.run(**options)
      abort "FAIL: accepted #{label}" if reason
      abort 'FAIL: direct return or original restoration' unless result[:status]=='unlocked_recovery_verified' &&
        File.binread(File.join(dir,'part2'))==original && count.call('write-count')==2 && count.call('reboot-count')==1
      A133Unlock::Stage.run(**options)
      abort 'FAIL: completed direct stage replayed effects' unless count.call('write-count')==2 && count.call('reboot-count')==1
    rescue A133Unlock::Stage::Invalid => error
      abort "FAIL: #{label}: expected #{reason || 'success'}, got #{error.message}" unless reason && error.message==reason
      abort 'FAIL: unknown direct environment was overwritten' unless count.call('write-count')==1 && count.call('reboot-count')==1
    end
    checks+=1
  end
end
puts "Direct unlock USB: #{checks} return, resume and unknown-state cases passed"
