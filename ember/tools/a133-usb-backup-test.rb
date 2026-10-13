#!/usr/bin/env ruby
# Origin: EmberBSD - actual-dd USB acquisition, host failure and redacted CLI contracts.
require 'open3'
require 'rbconfig'
require 'tmpdir'
require_relative 'a133-capture-test-support'
tool = File.join(__dir__,'a133-usb-backup.rb')
abort 'FAIL: USB backup collector is missing' unless File.file?(tool)
require tool
CaptureFixture.configure
module BackupHostFault
  def fsync
    fault = $backup_host_fault
    if fault && File.basename(path)=='emmc.raw.partial'
      $backup_host_fault = nil
      raise Errno::ENOSPC,'PRIVATE_HOST_DISK_DIAGNOSTIC' if fault==:fsync
      File.binwrite(File.join(File.dirname(path),'emmc.raw'),'FOREIGN_OUTPUT') if fault==:collision
    end
    super
  end
end
File.prepend(BackupHostFault)
checks = 0
Dir.mktmpdir('usb-backup-') do |parent|
  File.chmod(0700,parent)
  source = File.join(parent,'source'); Dir.mkdir(source,0700)
  adb = File.join(source,'adb')
  setup = lambda do |state={}|
    CaptureFixture.write(source)
    File.binwrite(File.join(source,'boot0.bin'),'e'*1024)
    File.binwrite(File.join(source,'boot1.bin'),'f'*1024)
    File.write(adb,File.read(File.join(__dir__,'a133-usb-backup-fixture.rb')))
    File.chmod(0700,adb)
    File.write(File.join(source,'state.json'),JSON.generate(state))
  end
  index = 0
  destination = -> {index+=1; File.join(parent,'backup-'+index.to_s)}
  run = ->(dir,**options) {A133UsbBackup.collect(directory:dir,adb:adb,timeout:30,**options)}
  setup.call
  saved_source = %w[disk.raw bootloader.bin env.bin boot.bin recovery.bin boot0.bin boot1.bin].to_h { |name| [name,Digest::SHA256.file(File.join(source,name)).hexdigest] }
  dir = destination.call
  result = run.call(dir)
  abort 'FAIL: incomplete/redacted acquisition receipt' unless result[:status]=='usb_backup_captured' &&
    result[:host_files_created]==8 && result[:saved_bytes]==296960 &&
    result[:hardware_boot_copies_verified]==true && result[:writes_performed]==0 &&
    result[:installation_ready]==false && result[:filesystem_consistency]=='not_established_by_integrity_check'
  abort 'FAIL: private receipt leaked' if JSON.generate(result).include?('CAPTURE_TEST_SERIAL') || JSON.generate(result).include?(parent)
  mapping = {'emmc.raw'=>'disk.raw','bootloader.bin'=>'bootloader.bin','env.bin'=>'env.bin',
    'boot.bin'=>'boot.bin','recovery.bin'=>'recovery.bin','boot0.bin'=>'boot0.bin','boot1.bin'=>'boot1.bin'}
  mapping.each do |output,input|
    abort 'FAIL: captured bytes/source changed' unless File.binread(File.join(dir,output))==File.binread(File.join(source,input)) &&
      Digest::SHA256.file(File.join(source,input)).hexdigest==saved_source.fetch(input)
    abort 'FAIL: unsafe published file' unless File.stat(File.join(dir,output)).mode&07777==0600
  end
  abort 'FAIL: unsafe capture directory/unfinished files' unless File.stat(dir).mode&07777==0700 &&
    Dir.children(dir).sort==(mapping.keys+['capture.json']).sort
  bound = A133Capture.verify(File.join(dir,'capture.json'),serial:'CAPTURE_TEST_SERIAL',cid:'0123456789abcdef0123456789abcdef')
  abort 'FAIL: collector manifest unusable by next stage' unless bound['hardware_boot_copies_verified']==true
  checks+=1
  reject = lambda do |reason,dir,&block|
    begin
      block.call
      abort "FAIL: accepted #{reason}"
    rescue A133UsbBackup::Invalid => error
      abort "FAIL: wanted #{reason}, got #{error.message}" unless error.message==reason
      abort 'FAIL: private library cause/output' unless error.cause.nil? &&
        !error.full_message.include?('PRIVATE_') && !error.message.include?(parent)
    end
    abort 'FAIL: failure published final manifest' if File.exist?(File.join(dir,'capture.json'))
    checks+=1
  end
  cases = [
    ['usb_backup_expected_one_device',{'duplicate'=>true}],
    ['usb_backup_device_not_ready',{'network'=>true}],
    ['usb_backup_device_not_ready',{'state'=>'offline'}],
    ['usb_shell_v2_required',{'features'=>'shell'}],
    ['usb_root_required',{'root'=>'2000'}],
    ['usb_board_mismatch',{'model'=>'other'}],
    ['usb_partition_layout_mismatch',{'numbers_bad'=>true}],
    ['usb_partition_mapping_mismatch',{'mapping_bad'=>true}],
    ['usb_backup_hardware_layout_mismatch',{'hardware_bad'=>true}],
    ['usb_backup_invalid_cid',{'cid'=>'bad'}],
    ['usb_backup_stream_size_mismatch',{'mode'=>'short'}],
    ['usb_backup_stream_size_mismatch',{'mode'=>'oversized'}],
    ['usb_command_failed',{'mode'=>'remote_error'}],
    ['usb_diagnostics',{'mode'=>'noisy'}],
    ['usb_backup_source_changed',{'mode'=>'cid_drift'}],
    ['critical_copy_hash_mismatch',{'mode'=>'copy_drift'}]
  ]
  cases.each do |reason,state|
    setup.call(state); dir=destination.call
    reject.call(reason,dir) {run.call(dir)}
  end
  setup.call('state'=>'recovery','mountinfo'=>"1 0 179:1 / /data rw - ext4 /dev/block/alias rw\n",'block_ids'=>['179:1'])
  dir=destination.call
  reject.call('usb_target_mounted',dir) {run.call(dir,state:'recovery',root_method:'adbd')}
  setup.call('state'=>'recovery')
  dir=destination.call
  result=run.call(dir,state:'recovery',root_method:'adbd')
  abort 'FAIL: recovery acquisition claims consistency' unless result[:status]=='usb_backup_captured' &&
    result[:filesystem_consistency]=='not_established_by_integrity_check'
  checks+=1
  setup.call
  dir=destination.call; Dir.mkdir(dir,0700); File.write(File.join(dir,'sentinel'),'KEEP')
  begin
    run.call(dir)
    abort 'FAIL: adopted existing destination'
  rescue A133UsbBackup::Invalid => error
    abort 'FAIL: destination replacement' unless error.message=='usb_backup_destination_exists' && File.read(File.join(dir,'sentinel'))=='KEEP'
    checks+=1
  end
  dir=destination.call
  File.chmod(0755,parent)
  reject.call('usb_backup_unsafe_parent',dir) {run.call(dir)}
  File.chmod(0700,parent)
  [:fsync,:collision].each do |fault|
    setup.call; dir=destination.call; $backup_host_fault=fault
    reason=fault==:fsync ? 'usb_backup_host_file_error' : 'usb_backup_output_exists'
    reject.call(reason,dir) {run.call(dir)}
    abort 'FAIL: partial evidence removed' unless File.exist?(File.join(dir,'emmc.raw.partial'))
    abort 'FAIL: collision overwritten' if fault==:collision && File.binread(File.join(dir,'emmc.raw'))!='FOREIGN_OUTPUT'
  end
  $backup_host_fault=nil
  setup.call('mode'=>'stall'); dir=destination.call
  started=Process.clock_gettime(Process::CLOCK_MONOTONIC)
  reject.call('usb_backup_timeout',dir) {A133UsbBackup.collect(directory:dir,adb:adb,timeout:1)}
  abort 'FAIL: deadline unbounded' unless Process.clock_gettime(Process::CLOCK_MONOTONIC)-started<5
  setup.call
  bootstrap=File.join(parent,'cli.rb')
  File.write(bootstrap,"require #{File.join(__dir__,'a133-capture-test-support.rb').inspect}\nCaptureFixture.configure\n$0=#{tool.inspect}\nload #{tool.inspect}\n")
  dir=destination.call
  out,err,status=Open3.capture3(RbConfig.ruby,bootstrap,'--adb',adb,dir)
  abort 'FAIL: CLI leaked/private or failed' unless status.success? && err.empty? && JSON.parse(out)['status']=='usb_backup_captured' &&
    !out.include?('CAPTURE_TEST_SERIAL') && !out.include?('0123456789abcdef') && !out.include?(parent)
  checks+=1
  out,err,status=Open3.capture3(RbConfig.ruby,tool,'--write',destination.call)
  abort 'FAIL: CLI write option' unless !status.success? && err.empty? && JSON.parse(out)['reason']=='invalid_arguments'
  checks+=1
end
puts "USB backup: #{checks} actual-dd acquisition and failure cases passed"
