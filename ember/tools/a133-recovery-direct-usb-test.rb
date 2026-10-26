#!/usr/bin/env ruby
# Origin: EmberBSD - direct-profile entry and restore with actual file-backed USB I/O.
require 'tmpdir'
require_relative 'a133-recovery-reboot-test-support'
require_relative 'a133-recovery-entry'
require_relative 'a133-recovery-reboot'
checks=0
Dir.mktmpdir('a133-direct-') do |dir|
  context=RebootFixture.create(dir,direct:true)
  File.write(File.join(dir,'adb-source.rb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-reboot-fixture.rb')))
  reset=lambda do |state={},live=context[:original]|
    RebootFixture.reset(dir,context,{'state'=>'device'}.merge(state),live)
    File.unlink(File.join(dir,'written')) if File.exist?(File.join(dir,'written'))
  end
  entry=lambda do |state='device'|
    A133Recovery::Entry.new(adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,
      cid:UsbFixture::CID,state:state,root_method:'adbd',timeout:5)
  end
  call=lambda {entry.call.arm(original_env:context[:original],backup:context[:backup])}
  reset.call
  result=call.call
  abort 'FAIL: direct arm bytes/receipt' unless result[:writes_performed]==1 &&
    result[:installation_ready]==false && File.binread(File.join(dir,'part2'))==context[:armed]
  File.unlink(File.join(dir,'written'))
  abort 'FAIL: direct arm repeat wrote' unless call.call[:writes_performed]==0 && !File.exist?(File.join(dir,'written'))
  checks+=2
  [context[:armed],context[:consumed],context[:original]].each do |live|
    reset.call({},live)
    result=entry.call.disarm(original_env:context[:original],backup:context[:backup])
    abort 'FAIL: direct disarm bytes/receipt' unless result[:status]=='env_entry_disarmed' &&
      result[:writes_performed]==(live==context[:original] ? 0 : 1) &&
      File.binread(File.join(dir,'part2'))==context[:original]
    checks+=1
  end
  reset.call({'state'=>'recovery'},context[:consumed])
  begin
    entry.call('recovery').disarm(original_env:context[:original],backup:context[:backup])
    abort 'FAIL: disarm used in recovery'
  rescue A133Usb::Invalid => error
    abort 'FAIL: disarm state gate' unless error.message=='usb_entry_wrong_state' && error.write_attempted==false
  end
  checks+=1
  reset.call({},context[:altered])
  begin
    entry.call.disarm(original_env:context[:original],backup:context[:backup])
    abort 'FAIL: unknown env disarmed'
  rescue A133Usb::Invalid => error
    abort 'FAIL: unknown disarm gate' unless error.message=='usb_environment_unknown' && error.write_attempted==false &&
      File.binread(File.join(dir,'part2'))==context[:altered] && !File.exist?(File.join(dir,'written'))
  end
  checks+=1
  reset.call({'state'=>'recovery'},context[:reordered])
  result=entry.call('recovery').restore(original_env:context[:original],backup:context[:backup])
  abort 'FAIL: direct consumed restoration' unless result[:writes_performed]==1 &&
    File.binread(File.join(dir,'part2'))==context[:original]
  checks+=1
  reset.call
  call.call
  File.unlink(File.join(dir,'written'))
  reboot=A133Recovery::Reboot.new(adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,
    cid:UsbFixture::CID,device_root_method:'adbd',recovery_root_method:'adbd',timeout:5,wait_timeout:2)
  result=reboot.enter_recovery(original_env:context[:original],backup:context[:backup])
  abort 'FAIL: direct reboot/readback contract' unless result[:status]=='factory_recovery_verified' &&
    result[:direct_writes_performed]==0 && result[:reboots_submitted]==1 &&
    File.binread(File.join(dir,'part2'))==context[:consumed] && !File.exist?(File.join(dir,'written'))
  entry.call('recovery').restore(original_env:context[:original],backup:context[:backup])
  File.unlink(File.join(dir,'written'))
  result=reboot.return_android(original_env:context[:original],backup:context[:backup])
  abort 'FAIL: direct Android return contract' unless result[:status]=='factory_android_verified' &&
    result[:installation_ready]==false && File.binread(File.join(dir,'part2'))==context[:original] &&
    %w[bootloader boot recovery].zip([1,3,6]).all? { |role,index| Digest::SHA256.file(File.join(dir,"part#{index}")).hexdigest==context[:backup]['partition_sha256'][role] }
  checks+=2
  [
    [{'model'=>'a133'},'usb_board_mismatch',false],
    [{'root'=>'2000'},'usb_root_required',false],
    [{'locked'=>'0','verified'=>'orange'},'usb_entry_state_unsupported',false],
    [{'swaps'=>"Filename Type Size Used Priority\n/dev/block/zram0 partition 1 0 -2\n"},'usb_target_in_use',false],
    [{'write_mode'=>'partial'},'usb_command_failed',true],
    [{'write_mode'=>'sync_failed'},'usb_command_failed',true],
    [{'write_mode'=>'tail_corrupt'},'usb_environment_readback_mismatch',true]
  ].each do |state,reason,attempted|
    reset.call(state)
    begin
      call.call; abort 'FAIL: unsafe direct entry accepted'
    rescue A133Usb::Invalid => error
      abort "FAIL: direct gate #{reason}/#{attempted}: #{error.message}/#{error.write_attempted}" unless
        error.message==reason && error.write_attempted==attempted && error.cause.nil?
    end
    abort 'FAIL: rejected direct preflight changed bytes' if !attempted &&
      (File.exist?(File.join(dir,'written')) || File.binread(File.join(dir,'part2'))!=context[:original])
    checks+=1
  end
  reset.call
  begin
    A133UsbBackup::Source.new(adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,state:'device',root_method:'adbd',timeout:5).inspect!
    abort 'FAIL: default mutation source admitted C3'
  rescue A133UsbBackup::Invalid => error
    abort 'FAIL: default mutation gate changed' unless error.message=='usb_board_mismatch'
  end
  checks+=1
end
puts "Direct recovery USB: #{checks} file-backed entry/restore/reboot cases passed"
