#!/usr/bin/env ruby
# Origin: EmberBSD - actual-process factory reboot round trips and conservative failure flags.
require 'tmpdir'
require 'rbconfig'
require_relative 'a133-recovery-reboot-test-support'
abort 'FAIL: reboot API missing' unless File.file?(File.join(__dir__,'a133-recovery-reboot.rb'))
require_relative 'a133-recovery-reboot'
require_relative 'a133-recovery-entry'
checks=0
Dir.mktmpdir('a133-reboot-') do |dir|
  context=RebootFixture.create(dir)
  File.write(File.join(dir,'adb-source.rb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-reboot-fixture.rb')))
  original_gpt=File.open(File.join(dir,'disk'),'rb') { |f| head=f.read(17408); f.seek(31037849600-17408); [head,f.read(17408)] }
  reset=lambda do |state={},live=context[:armed]|
    RebootFixture.reset(dir,context,{'state'=>'device'}.merge(state),live)
    File.open(File.join(dir,'disk'),'r+b') { |f| f.write(original_gpt[0]); f.seek(31037849600-17408); f.write(original_gpt[1]) }
  end
  api=lambda do |options={}|
    A133Recovery::Reboot.new(adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,
      timeout:3,wait_timeout:1,**options)
  end
  call=lambda do |action=:enter,options={},backup=context[:backup],env=context[:original]|
    instance=api.call(options)
    action==:enter ? instance.enter_recovery(original_env:env,backup:backup) : instance.return_android(original_env:env,backup:backup)
  end
  reboot_count=lambda { File.exist?(File.join(dir,'rebooted')) ? File.readlines(File.join(dir,'rebooted')).size : 0 }
  failed=lambda do |reason,attempted,action=:enter,options={},backup=context[:backup],env=context[:original]|
    before=Digest::SHA256.file(File.join(dir,'part2')).hexdigest
    begin
      call.call(action,options,backup,env); abort 'FAIL: unsafe reboot accepted '+reason
    rescue A133Recovery::Reboot::Invalid => error
      abort "FAIL: expected #{reason}/#{attempted}, got #{error.message}/#{error.reboot_attempted}" unless
        error.message==reason && error.reboot_attempted==attempted && error.write_attempted==attempted && error.cause.nil? &&
        !error.full_message.include?(dir) && !error.full_message.include?('PRIVATE_')
    end
    abort 'FAIL: raw write or repeated reboot' if File.exist?(File.join(dir,'written')) || reboot_count.call!=(attempted ? 1 : 0)
    abort 'FAIL: preflight changed env' if !attempted && Digest::SHA256.file(File.join(dir,'part2')).hexdigest!=before
    checks+=1
  end
  unless ARGV==['--tail']
    # Full writer/readback/native-process integration, with exact original restoration.
    reset.call({},context[:original])
    entry=A133Recovery::Entry.new(adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,state:'device')
    abort 'FAIL: arm did not write once' unless entry.arm(original_env:context[:original],backup:context[:backup])[:writes_performed]==1
    File.unlink(File.join(dir,'written'))
    state=JSON.parse(File.read(File.join(dir,'state.json'))); state['sequence']=%w[missing offline device recovery]
    File.write(File.join(dir,'state.json'),JSON.generate(state))
    result=call.call
    abort 'FAIL: enter receipt or consumed save' unless result[:status]=='factory_recovery_verified' && result[:direct_writes_performed]==0 &&
      result[:reboots_submitted]==1 && result[:storage_side_effects_possible]==true && result[:installation_ready]==false &&
      result[:destination][:state]=='recovery' && File.binread(File.join(dir,'part2'))==context[:consumed] && reboot_count.call==1 &&
      !File.exist?(File.join(dir,'written')) && File.read(File.join(dir,'polls')).lines.map(&:strip).first(4)==%w[missing offline device recovery]
    restore=A133Recovery::Entry.new(adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,state:'recovery',root_method:'adbd')
    abort 'FAIL: restore did not write once' unless restore.restore(original_env:context[:original],backup:context[:backup])[:writes_performed]==1
    File.unlink(File.join(dir,'written'))
    File.unlink(File.join(dir,'rebooted'))
    state=JSON.parse(File.read(File.join(dir,'state.json'))); state['reboot_started']=false; state['sequence']=%w[offline recovery device]
    File.write(File.join(dir,'state.json'),JSON.generate(state))
    result=call.call(:return)
    abort 'FAIL: return receipt/env/critical preservation' unless result[:status]=='factory_android_verified' && result[:destination][:state]=='device' &&
      result[:direct_writes_performed]==0 && result[:reboots_submitted]==1 && result[:installation_ready]==false &&
      File.binread(File.join(dir,'part2'))==context[:original] && reboot_count.call==1 && !File.exist?(File.join(dir,'written')) &&
      %w[bootloader boot recovery].zip([1,3,6]).all? { |role,index| Digest::SHA256.file(File.join(dir,"part#{index}")).hexdigest==context[:backup]['partition_sha256'][role] }
    checks+=2
    reset.call({},context[:original]); failed.call('usb_environment_unexpected',false)
    reset.call({'state'=>'recovery'},context[:consumed]); failed.call('usb_environment_unexpected',false,:return)
    reset.call; File.open(File.join(dir,'part3'),'r+b') { |f| f.write('X') }; failed.call('usb_boot_changed',false)
    reset.call({'state'=>'recovery'},context[:original]); File.open(File.join(dir,'part3'),'r+b') { |f| f.write('X') }; failed.call('usb_boot_changed',false,:return)
    reset.call({'state'=>'recovery','late'=>{'corrupt_role'=>'boot'}},context[:original]); failed.call('usb_boot_changed',true,:return)
    [
      [{'cid'=>'f'*32},'usb_readback_identity_mismatch'],
      [{'root'=>'2000'},'usb_root_required'],
      [{'locked'=>'0','verified'=>'orange'},'usb_readback_state_unsupported'],
      [{'holders_unreadable'=>true},'usb_command_failed']
    ].each { |state,reason| reset.call(state); failed.call(reason,false) }
    [
      [{'cid'=>'f'*32},'usb_readback_identity_mismatch'],
      [{'root'=>'2000'},'usb_root_required'],
      [{'locked'=>'0','verified'=>'orange'},'usb_readback_state_unsupported'],
      [{'holders'=>true},'usb_target_in_use'],
      [{'holders_unreadable'=>true},'usb_command_failed'],
      [{'transport'=>'tcp'},'usb_reboot_transport_required'],
      [{'duplicate'=>true},'usb_reboot_duplicate_device'],
      [{'gpt_changed'=>true},'usb_readback_gpt_mismatch'],
      [{'corrupt_role'=>'boot'},'usb_boot_changed'],
      [{'corrupt_role'=>'bootloader'},'usb_bootloader_changed'],
      [{'corrupt_role'=>'recovery'},'usb_recovery_changed'],
      [{'corrupt_role'=>'env'},'usb_environment_unexpected'],
      [{'tail_corrupt'=>true},'usb_environment_unexpected'],
      [{'hardware_sectors'=>4096},'usb_reboot_hardware_changed'],
      [{'read_mode'=>'short'},'usb_backup_stream_size_mismatch']
    ].each { |late,reason| reset.call('late'=>late); failed.call(reason,true) }
    %w[unauthorized bootloader].each do |state|
      reset.call('sequence'=>[state]); failed.call('usb_reboot_device_not_ready',true)
    end
    %w[device missing offline].each do |state|
      reset.call('sequence'=>[state]*100)
      failed.call('usb_reboot_wait_timeout',true)
      submitted=Float(File.read(File.join(dir,'reboot-clock')))
      elapsed=Process.clock_gettime(Process::CLOCK_MONOTONIC)-submitted
      abort 'FAIL: inventory wait unbounded' unless elapsed.between?(0,5)
    end
  end
  [['failure','usb_command_failed'],['noisy','usb_diagnostics'],['stall','usb_timeout']].each do |mode,reason|
    reset.call('reboot_mode'=>mode); failed.call(reason,true,:enter,{timeout:3})
  end
  reset.call('late'=>{'inventory_stall'=>true}); failed.call('usb_timeout',true,:enter,{timeout:3})
  reset.call
  bad=Marshal.load(Marshal.dump(context[:backup])); bad['partition_sha256']['boot']='x'
  failed.call('invalid_entry_backup',false,:enter,{},bad)
  [[{wait_timeout:0},'invalid_reboot_options'],[{timeout:0},'invalid_usb_options'],[{serial:'x:y'},'invalid_usb_identity'],
    [{device_root_method:'guess'},'invalid_arguments'],[{recovery_root_method:'guess'},'invalid_arguments'],
    [{adb:"bad\0path"},'invalid_reboot_options']].each do |options,reason|
    reset.call; failed.call(reason,false,:enter,options)
  end
  reset.call
  serial=RebootFixture::SERIAL.dup; cid=UsbFixture::CID.dup; adb=File.join(dir,'adb'); root='vendor_su'; recovery_root='adbd'
  b=Marshal.load(Marshal.dump(context[:backup])); env=context[:original].dup
  instance=api.call(serial:serial,cid:cid,adb:adb,device_root_method:root,recovery_root_method:recovery_root)
  mutated=false
  worker=Thread.new do
    sleep 0.005 until File.exist?(File.join(dir,'rebooted'))
    serial.replace('OTHER'); cid.replace('f'*32); adb.replace('PRIVATE_LOCATION'); root.replace('guess'); recovery_root.replace('guess')
    b['partition_sha256'].values.each { |value| value.replace('0'*64) }; b['gpt_sha256'].replace('0'*64); env.setbyte(16777215,0)
    mutated=true
  end
  begin
    result=instance.enter_recovery(original_env:env,backup:b)
    abort 'FAIL: offline-epoch caller mutation affected identity/evidence' unless mutated && result[:status]=='factory_recovery_verified' && reboot_count.call==1
  ensure
    worker.kill; worker.join
  end
  checks+=1
  launch=File.join(dir,'launch')
  reset.call; File.write(launch,'PRIVATE_HOST_LOCATION'); failed.call('usb_readback_io_failed',false,:enter,{adb:File.join(launch,'adb')})
  File.unlink(launch); Dir.mkdir(launch)
  File.write(File.join(launch,'adb'),"#!#{RbConfig.ruby}\nexec #{File.join(dir,'adb').inspect}, *ARGV\n")
  File.chmod(0700,File.join(launch,'adb'))
  reset.call('reboot_mode'=>'launcher_invalid'); failed.call('usb_reboot_io_failed',true,:enter,{adb:File.join(launch,'adb')})
end
puts "Recovery reboot: #{checks} actual-process transitions/flags cases passed"
