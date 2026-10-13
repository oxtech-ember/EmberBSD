#!/usr/bin/env ruby
# Origin: EmberBSD - guarded one-shot entry/restoration with independent env and actual dd.
require 'tmpdir'
require 'rbconfig'
require_relative 'a133-usb-test-support'
tool=File.join(__dir__,'a133-recovery-entry.rb')
abort 'FAIL: recovery entry primitive is missing' unless File.file?(tool)
require tool
serial='PROTECTION_TEST_SERIAL'
vars={'opaque'=>"\xffKEEP=this".b+('KEEP_VALUE'*80),
  'bootcmd'=>'run setargs_mmc boot_normal','boot_normal'=>'run ${hook};run boot_android',
  'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000','bootdelay'=>'0'}
helpers={'hook'=>'ember_restore_normal ember_recovery_once',
  'ember_restore_normal'=>'setenv hook; saveenv',
  'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'}
encode=lambda do |values,leading=false,pad="\xff".b|
  body=(leading ? "\0".b : ''.b)+values.map { |key,value| key.b+'='+value.b+"\0" }.join+"\0"
  body=body.ljust(131068,pad); [Zlib.crc32(body)].pack('V')+body
end
tail=("\0\r\n\xffTAIL_KEEP".b*1400000).byteslice(0,16777216-131072)
original=encode.call(vars)+tail
armed=encode.call(vars.merge(helpers))+tail
consumed=vars.merge(helpers.reject { |key,_| key=='hook' })
checks=0
[["\xff".b.force_encoding('UTF-8'),UsbFixture::CID],[serial,"\xff".b.force_encoding('UTF-8')],[nil,UsbFixture::CID]].each do |bad_serial,bad_cid|
  begin
    A133Recovery::Entry.new(adb:'/unused/PRIVATE_LAUNCHER',serial:bad_serial,cid:bad_cid,state:'device')
    abort 'FAIL: malformed identity accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: malformed identity was not normalized before USB' unless error.message=='invalid_usb_identity' && error.write_attempted==false && error.cause.nil?
  end
  checks+=1
end
Dir.mktmpdir('a133-entry-') do |dir|
  UsbFixture.create(dir)
  File.write(File.join(dir,'layout.json'),JSON.generate(UsbFixture::PARTS))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  bootloader=File.binread(File.join(dir,'part1')); recovery=File.binread(File.join(dir,'part6'))
  disk=File.open(File.join(dir,'disk'),'rb') { |f| head=f.read(17408); f.seek(31037849600-17408); head+f.read(17408) }
  backup=UsbFixture.backup(dir).merge('serial'=>serial,'critical_copies_verified'=>true,
    'hardware_boot_copies_verified'=>true,'gpt_sha256'=>Digest::SHA256.hexdigest(disk))
  backup['partition_sha256']['env']=Digest::SHA256.hexdigest(original)
  backup['partition_sha256']['bootloader']=Digest::SHA256.hexdigest(bootloader)
  reset=lambda do |state={},live=original|
    File.binwrite(File.join(dir,'part2'),live)
    File.binwrite(File.join(dir,'part1'),bootloader); File.binwrite(File.join(dir,'part6'),recovery)
    UsbFixture.state(dir,state)
    File.unlink(File.join(dir,'observed')) if File.exist?(File.join(dir,'observed'))
  end
  call=lambda do |action=:arm,b=backup,env=original,options={}|
    A133Recovery::Entry.new(adb:File.join(dir,'adb'),serial:serial,cid:UsbFixture::CID,
      state:action==:arm ? 'device' : 'recovery',**options).public_send(action,original_env:env,backup:b)
  end
  failed=lambda do |reason,attempted,action=:arm,b=backup,env=original,options={}|
    before=File.binread(File.join(dir,'part2'))
    begin
      call.call(action,b,env,options); abort 'FAIL: unsafe entry accepted '+reason
    rescue A133Usb::Invalid => error
      abort "FAIL: expected #{reason}/#{attempted}, got #{error.message}/#{error.write_attempted.inspect}" unless
        error.message==reason && error.write_attempted==attempted && error.cause.nil?
      abort 'FAIL: private error leaked' if error.full_message.include?('PRIVATE_') || error.full_message.include?(dir)
    end
    abort 'FAIL: preflight wrote storage' unless attempted || (File.binread(File.join(dir,'part2'))==before && !File.exist?(File.join(dir,'written')))
    checks+=1
  end
  %w[adbd vendor_su].each do |method|
    reset.call({'state'=>'device','mountinfo'=>"1 0 0:1 / / rw - rootfs rootfs rw\n2 1 179:17 / /data rw - ext4 /dev/block/mmcblk0p17 rw\n"})
    result=call.call(:arm,backup,original,{root_method:method})
    abort 'FAIL: incorrect armed prefix/tail/receipt' unless File.binread(File.join(dir,'part2'))==armed && result[:status]=='env_entry_armed' &&
      result[:env_sha256]==Digest::SHA256.hexdigest(armed) && result[:writes_performed]==1 && result[:installation_ready]==false &&
      File.binread(File.join(dir,'part1'))==bootloader && File.binread(File.join(dir,'part6'))==recovery
    checks+=1
    File.unlink(File.join(dir,'written'))
    again=call.call(:arm,backup,original,{root_method:method})
    abort 'FAIL: arm repeat wrote env' unless again[:writes_performed]==0 && !File.exist?(File.join(dir,'written'))
    checks+=1
  end
  [armed,encode.call(consumed)+tail,encode.call(consumed.to_a.reverse.to_h,true,"\0")+tail,original].each do |live|
    reset.call({},live)
    result=call.call(:restore)
    abort 'FAIL: restoration/receipt incorrect' unless File.binread(File.join(dir,'part2'))==original && result[:status]=='env_entry_restored' &&
      result[:writes_performed]==(live==original ? 0 : 1) && result[:env_sha256]==Digest::SHA256.hexdigest(original) && result[:installation_ready]==false &&
      File.binread(File.join(dir,'part1'))==bootloader && File.binread(File.join(dir,'part6'))==recovery
    checks+=1
  end
  %w[serial cid critical_copies_verified hardware_boot_copies_verified gpt_headers_crc gpt_arrays_crc partition_layout_verified bytes gpt_sha256 uncompressed_sha256].each do |key|
    reset.call('state'=>'device'); b=Marshal.load(Marshal.dump(backup)); b[key]=false
    failed.call('invalid_entry_backup',false,:arm,b)
  end
  reset.call('state'=>'device'); b=Marshal.load(Marshal.dump(backup)); b['partition_sha256'][:extra]='a'*64
  failed.call('invalid_entry_backup',false,:arm,b)
  reset.call('state'=>'device'); b=Marshal.load(Marshal.dump(backup)); b['gpt_sha256']="\xff".b.force_encoding('UTF-8')
  failed.call('invalid_entry_backup',false,:arm,b)
  reset.call('state'=>'device'); failed.call('original_environment_size_mismatch',false,:arm,backup,original[0...-1])
  reset.call('state'=>'device'); failed.call('original_environment_hash_mismatch',false,:arm,backup,original.sub('TAIL_KEEP','TAIL_FAIL'))
  [
    [{'state'=>'recovery'},'usb_backup_device_not_ready'],
    [{'state'=>'device','root'=>'2000'},'usb_root_required'],
    [{'state'=>'device','cid'=>'f'*32},'usb_entry_identity_mismatch'],
    [{'state'=>'device','locked'=>'0','verified'=>'orange'},'usb_entry_state_unsupported'],
    [{'state'=>'device','verified'=>'orange'},'usb_entry_state_unsupported'],
    [{'state'=>'device','holders'=>true},'usb_target_in_use'],
    [{'state'=>'device','thread_mountinfo'=>"2 1 179:2 / /alias rw - ext4 alias rw\n"},'usb_target_mounted'],
    [{'state'=>'device','swaps'=>"Filename Type Size Used Priority\n/dev/block/zram0 partition 1 0 -2\n"},'usb_target_in_use'],
    [{'state'=>'device','prewrite_mount'=>true},'usb_target_mounted'],
    [{'state'=>'device','read_mode'=>'short'},'usb_backup_stream_size_mismatch'],
    [{'state'=>'device','read_mode'=>'noisy'},'usb_diagnostics'],
    [{'state'=>'device','read_mode'=>'failed'},'usb_command_failed']
  ].each { |state,reason| reset.call(state); failed.call(reason,false) }
  reset.call('state'=>'device'); failed.call('usb_entry_wrong_state',false,:arm,backup,original,{state:'recovery'})
  reset.call; failed.call('usb_entry_wrong_state',false,:restore,backup,original,{state:'device'})
  reset.call('state'=>'device'); failed.call('usb_entry_gpt_mismatch',false,:arm,backup.merge('gpt_sha256'=>'1'*64))
  reset.call('state'=>'device'); File.open(File.join(dir,'part1'),'r+b') { |f| f.write('X') }; failed.call('usb_bootloader_changed',false)
  reset.call('state'=>'device'); File.open(File.join(dir,'part6'),'r+b') { |f| f.write('X') }; failed.call('usb_recovery_changed',false)
  reset.call({'state'=>'device'},encode.call(consumed)+tail); failed.call('usb_environment_unknown',false)
  [consumed.merge('bootdelay'=>'3'),consumed.merge('surprise'=>'value'),consumed.merge('hook'=>helpers['hook']),consumed.merge('ember_restore_normal'=>'saveenv')].each do |unknown|
    reset.call({},encode.call(unknown)+tail); failed.call('usb_environment_unknown',false,:restore)
  end
  reset.call({},encode.call(consumed)+tail.sub('TAIL_KEEP','TAIL_FAIL')); failed.call('usb_environment_unknown',false,:restore)
  reset.call({},(encode.call(consumed)+tail).sub('bootdelay','BOOTDELAY')); failed.call('usb_environment_unknown',false,:restore)
  [
    ['partial','usb_command_failed'],['sync_failed','usb_command_failed'],
    ['tail_corrupt','usb_environment_readback_mismatch'],['recovery_corrupt','usb_recovery_changed'],
    ['bootloader_corrupt','usb_bootloader_changed'],['cid_drift','usb_backup_source_changed'],
    ['late_mount','usb_target_mounted'],['stall','usb_timeout']
  ].each do |mode,reason|
    reset.call('state'=>'device','write_mode'=>mode)
    failed.call(reason,true,:arm,backup,original,{timeout:1})
  end
  reset.call('state'=>'device','write_mode'=>'partial'); failed.call('usb_command_failed',true)
  UsbFixture.state(dir,'state'=>'device'); failed.call('usb_environment_unknown',false)
  reset.call('state'=>'device','write_mode'=>'sync_failed'); failed.call('usb_command_failed',true)
  UsbFixture.state(dir,'state'=>'device'); result=call.call
  abort 'FAIL: exact manual repeat wrote again' unless result[:writes_performed]==0 && !File.exist?(File.join(dir,'written'))
  checks+=1
  reset.call('state'=>'device')
  b=Marshal.load(Marshal.dump(backup)); env=original.dup
  worker=Thread.new do
    sleep 0.005 until File.exist?(File.join(dir,'observed'))
    b['partition_sha256']['env'].replace('0'*64); b['partition_sha256']['bootloader'].replace('0'*64)
    b['partition_sha256']['recovery'].replace('0'*64); b['gpt_sha256'].replace('0'*64)
    env.setbyte(16777215,env.getbyte(16777215)^0xff)
  end
  result=call.call(:arm,b,env); worker.join
  abort 'FAIL: caller mutation changed pinned policy' unless result[:writes_performed]==1 && File.binread(File.join(dir,'part2'))==armed
  checks+=1
  # Host launcher errors must preserve the exact before/after submission boundary.
  launch=File.join(dir,'launch')
  reset.call('state'=>'device'); File.write(launch,'PRIVATE_HOST_LOCATION')
  failed.call('usb_entry_io_failed',false,:arm,backup,original,{adb:File.join(launch,'adb')})
  File.unlink(launch); Dir.mkdir(launch)
  File.write(File.join(launch,'adb'),"#!#{RbConfig.ruby}\nexec #{File.join(dir,'adb').inspect}, *ARGV\n")
  File.chmod(0700,File.join(launch,'adb'))
  reset.call('state'=>'device','write_mode'=>'launcher_invalid')
  failed.call('usb_entry_io_failed',true,:arm,backup,original,{adb:File.join(launch,'adb')})
  abort 'FAIL: launcher fault did not follow actual write' unless File.binread(File.join(dir,'part2'))==armed
  # A valid new disk GUID with unchanged partition geometry must reject the old receipt.
  reset.call('state'=>'device')
  File.open(File.join(dir,'disk'),'r+b') do |f|
    [1,60620799].each do |lba|
      f.seek(lba*512); header=f.read(512); header[56,16]='G'*16; header[16,4]=[0].pack('V')
      header[16,4]=[Zlib.crc32(header.byteslice(0,92))].pack('V'); f.seek(lba*512); f.write(header)
    end
  end
  failed.call('usb_entry_gpt_mismatch',false)
end
puts "Recovery entry USB: #{checks} actual-dd cases passed"
