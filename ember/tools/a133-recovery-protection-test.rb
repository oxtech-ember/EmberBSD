#!/usr/bin/env ruby
# Origin: EmberBSD - real-dd recovery protection transition and failure contracts.
require 'tmpdir'
require 'zlib'
require_relative 'a133-usb-test-support'
tool=File.join(__dir__,'a133-recovery-protection.rb')
abort 'FAIL: recovery protection primitive is missing' unless File.file?(tool)
require tool
serial='PROTECTION_TEST_SERIAL'
vars={
  'opaque'=>"\xffkeep=this".b+('RETAINED_VALUE'*80),
  'bootcmd'=>'run setargs_mmc boot_normal','boot_normal'=>'run ${hook};run boot_android',
  'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000',
  'bootdelay'=>'0'
}
body=(vars.map { |key,value| key.b+'='+value.b+"\0" }.join+"\0").ljust(131068,"\xff".b)
prefix=[Zlib.crc32(body)].pack('V')+body
original=prefix+("\0\r\n\xffTAIL_KEEP".b*1400000).byteslice(0,16777216-131072)
abort 'FAIL: test env size' unless original.bytesize==16777216
checks=0
Dir.mktmpdir('a133-protection-') do |dir|
  UsbFixture.create(dir)
  File.binwrite(File.join(dir,'part2'),original)
  File.write(File.join(dir,'layout.json'),JSON.generate(UsbFixture::PARTS))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  recovery=File.binread(File.join(dir,'part6'))
  disk=File.open(File.join(dir,'disk'),'rb') do |file|
    head=file.read(17408); file.seek(31037849600-17408); head+file.read(17408)
  end
  backup=UsbFixture.backup(dir).merge('serial'=>serial,'critical_copies_verified'=>true,
    'hardware_boot_copies_verified'=>true)
  backup['partition_sha256']['env']=Digest::SHA256.hexdigest(original)
  mutable={'status'=>'mutable_integrity_verified','serial'=>serial,'cid'=>UsbFixture::CID,
    'gpt_sha256'=>Digest::SHA256.hexdigest(disk),'mutable_copies_verified'=>true,
    'observation'=>'recovery_unmounted_two_matching_reads',
    'partition_sha256'=>{'UDISK'=>'e'*64,'metadata'=>'f'*64},
    'filesystem_consistency'=>'not_established_by_integrity_check'}
  reset=lambda do |state={}|
    File.binwrite(File.join(dir,'part2'),original)
    File.binwrite(File.join(dir,'part6'),recovery)
    UsbFixture.state(dir,state)
    File.unlink(File.join(dir,'observed')) if File.exist?(File.join(dir,'observed'))
  end
  install=lambda do |b=backup,m=mutable,env=original,options={}|
    A133Recovery::Protection.new(adb:File.join(dir,'adb'),serial:serial,cid:UsbFixture::CID,
      **options).install(original_env:env,backup:b,mutable:m)
  end
  failed=lambda do |reason,attempted,b=backup,m=mutable,env=original,options={}|
    before_env=File.binread(File.join(dir,'part2'))
    before_recovery=Digest::SHA256.file(File.join(dir,'part6')).hexdigest
    begin
      install.call(b,m,env,options)
      abort 'FAIL: unsafe protection accepted '+reason
    rescue A133Usb::Invalid => error
      abort "FAIL: expected #{reason}/#{attempted}, got #{error.message}/#{error.write_attempted.inspect}" unless
        error.message==reason && error.write_attempted==attempted && error.cause.nil?
      abort 'FAIL: private diagnostics leaked' if error.full_message.include?('PRIVATE_') || error.full_message.include?(dir)
    end
    unless attempted
      abort 'FAIL: preflight changed storage' unless File.binread(File.join(dir,'part2'))==before_env &&
        Digest::SHA256.file(File.join(dir,'part6')).hexdigest==before_recovery && !File.exist?(File.join(dir,'written'))
    end
    checks+=1
  end
  reset.call
  mixed=Marshal.load(Marshal.dump(backup)); mixed['partition_sha256'][:extra]='a'*64
  failed.call('invalid_protection_backup',false,mixed)
  reset.call
  mixed=Marshal.load(Marshal.dump(mutable)); mixed['partition_sha256'][:extra]='a'*64
  failed.call('invalid_protection_mutable',false,backup,mixed)
  [{},{'locked'=>'0','verified'=>'orange'}].each do |state|
    %w[adbd vendor_su].each do |method|
      reset.call(state)
      result=install.call(backup,mutable,original,{root_method:method})
      live=File.binread(File.join(dir,'part2'))
      vars_after=A133Env.decode(live.byteslice(0,131072))[:entries].to_h
      expected=vars.merge('boot_normal'=>'run ember_recovery_once',
        'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery')
      abort 'FAIL: incorrect env transition' unless vars_after==expected &&
        live.byteslice(131072,16777216-131072)==original.byteslice(131072,16777216-131072) &&
        File.binread(File.join(dir,'part6'))==recovery && result[:status]=='recovery_protection_verified' &&
        result[:writes_performed]==1 && result[:installation_ready]==false &&
        result[:protected_env]==live.byteslice(0,131072) && result[:env_sha256]==Digest::SHA256.hexdigest(live)
      File.unlink(File.join(dir,'written'))
      again=install.call(backup,mutable,original,{root_method:method})
      abort 'FAIL: idempotence rewrote env' unless again[:writes_performed]==0 &&
        again[:env_sha256]==result[:env_sha256] && !File.exist?(File.join(dir,'written'))
      checks+=2
    end
  end
  %w[serial cid critical_copies_verified hardware_boot_copies_verified].each do |key|
    reset.call
    b=Marshal.load(Marshal.dump(backup)); b[key]=false
    failed.call('invalid_protection_backup',false,b)
  end
  %w[serial cid mutable_copies_verified observation gpt_sha256].each do |key|
    reset.call
    m=mutable.merge(key=>false)
    failed.call('invalid_protection_mutable',false,backup,m)
  end
  reset.call
  failed.call('original_environment_hash_mismatch',false,backup,mutable,original.sub('TAIL_KEEP','TAIL_FAIL'))
  reset.call
  failed.call('original_environment_size_mismatch',false,backup,mutable,original[0...-1])
  [
    [{'state'=>'device'},'usb_backup_device_not_ready'],
    [{'root'=>'2000'},'usb_root_required'],
    [{'cid'=>'f'*32},'usb_protection_identity_mismatch'],
    [{'size'=>1},'usb_partition_layout_mismatch'],
    [{'locked'=>'1','verified'=>'orange'},'usb_protection_state_unsupported'],
    [{'locked'=>'0','verified'=>'green'},'usb_protection_state_unsupported'],
    [{'mountinfo'=>"2 1 179:2 / /alias rw - ext4 /dev/block/alias rw\n",'block_ids'=>['179:2']},'usb_target_mounted'],
    [{'inventory_unreadable'=>true},'usb_command_failed'],
    [{'swaps'=>"Filename Type Size Used Priority\n/dev/block/mmcblk0p2 partition 1 0 -2\n"},'usb_target_in_use'],
    [{'holders'=>true},'usb_target_in_use'],
    [{'prewrite_mount'=>true},'usb_target_mounted'],
    [{'read_mode'=>'short'},'usb_backup_stream_size_mismatch'],
    [{'read_mode'=>'noisy'},'usb_diagnostics'],
    [{'read_mode'=>'failed'},'usb_command_failed']
  ].each { |state,reason| reset.call(state); failed.call(reason,false) }
  reset.call
  failed.call('usb_protection_gpt_mismatch',false,backup,mutable.merge('gpt_sha256'=>'1'*64))
  reset.call
  File.open(File.join(dir,'part2'),'r+b') { |file| file.seek(16777215); file.write('X') }
  failed.call('usb_environment_unknown',false)
  reset.call
  File.open(File.join(dir,'part6'),'r+b') { |file| file.write('X') }
  failed.call('usb_recovery_changed',false)
  reset.call
  install.call
  UsbFixture.state(dir)
  other=original.sub('TAIL_KEEP','TAIL_FAIL')
  b=Marshal.load(Marshal.dump(backup)); b['partition_sha256']['env']=Digest::SHA256.hexdigest(other)
  failed.call('usb_environment_unknown',false,b,mutable,other)
  reset.call('write_mode'=>'partial')
  failed.call('usb_command_failed',true)
  UsbFixture.state(dir)
  failed.call('usb_environment_unknown',false)
  [
    ['partial','usb_command_failed'],['sync_failed','usb_command_failed'],
    ['tail_corrupt','usb_environment_readback_mismatch'],['recovery_corrupt','usb_recovery_changed'],
    ['cid_drift','usb_backup_source_changed'],['late_mount','usb_target_mounted'],['stall','usb_timeout']
  ].each { |mode,reason| reset.call('write_mode'=>mode); failed.call(reason,true,backup,mutable,original,{timeout:3}) }
  reset.call
  b=Marshal.load(Marshal.dump(backup)); m=Marshal.load(Marshal.dump(mutable)); env=original.dup
  worker=Thread.new do
    sleep 0.005 until File.exist?(File.join(dir,'observed'))
    b['partition_sha256']['env'].replace('0'*64)
    b['partition_sha256']['recovery'].replace('0'*64)
    m['gpt_sha256'].replace('0'*64)
    env.setbyte(16777215,env.getbyte(16777215)^0xff)
  end
  result=install.call(b,m,env)
  worker.join
  abort 'FAIL: caller mutation changed captured policy' unless result[:writes_performed]==1
  checks+=1
end
puts "Recovery protection USB: #{checks} actual-dd cases passed"
