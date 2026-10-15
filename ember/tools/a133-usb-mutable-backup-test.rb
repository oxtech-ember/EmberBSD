#!/usr/bin/env ruby
# Origin: EmberBSD - actual-dd recovery copies, changing source and late host-evidence contracts.
require 'tmpdir'
require 'open3'
require 'rbconfig'
require_relative 'a133-mutable-test-support'
tool=File.join(__dir__,'a133-usb-mutable-backup.rb')
abort 'FAIL: mutable acquisition is missing' unless File.file?(tool)
require tool
MutableFixture.configure
module MutableLateSourceFault
  def mutable_inspect!
    result=super
    fault=$mutable_source_fault
    if fault && File.exist?(File.join(fault[:directory],'mutable.json.partial'))
      $mutable_source_fault=nil
      if fault[:kind]==:directory
        File.chmod(0755,fault[:directory])
      elsif fault[:kind]==:file
        File.binwrite(File.join(fault[:directory],'udisk.raw'),'X'*102912)
      else
        $mutable_publish_fault=fault
      end
    end
    result
  end
end
A133UsbBackup::Source.prepend(MutableLateSourceFault)
module MutablePublicationFault
  def fsync
    result=super
    fault=$mutable_publish_fault
    if fault && File.identical?(path,fault[:directory]) && File.exist?(File.join(path,'mutable.json'))
      $mutable_publish_fault=nil
      file=File.join(path,'mutable.json'); time=File.stat(file).mtime
      File.binwrite(file,File.binread(file).sub('0123456789abcdef0123456789abcdef','ffffffffffffffffffffffffffffffff'))
      File.utime(time,time,file)
    end
    if $mutable_fsync_fault && File.basename(path)=='udisk.raw.partial'
      fault=$mutable_fsync_fault; $mutable_fsync_fault=nil
      raise Errno::ENOSPC,'PRIVATE_HOST_DIAGNOSTIC' if fault==:fsync
      File.binwrite(File.join(File.dirname(path),'udisk.raw'),'FOREIGN') if fault==:collision
    end
    result
  end
end
File.prepend(MutablePublicationFault)
checks=0
Dir.mktmpdir('usb-mutable-') do |parent|
  File.chmod(0700,parent); source=File.join(parent,'source')
  setup=lambda do |state={}|
    MutableFixture.write(source)
    adb=File.join(source,'adb'); File.write(adb,File.read(File.join(__dir__,'a133-usb-mutable-fixture.rb'))); File.chmod(0700,adb)
    File.write(File.join(source,'state.json'),JSON.generate(state)); adb
  end
  adb=setup.call; index=0
  destination=-> {index+=1; File.join(parent,'copy-'+index.to_s)}
  run=->(dir,**options) {A133UsbBackup.collect_mutable(directory:dir,serial:'MUTABLE_TEST_SERIAL',cid:'0123456789abcdef0123456789abcdef',adb:adb,timeout:30,**options)}
  dir=destination.call; originals=%w[udisk.raw metadata.raw disk.raw].to_h { |name| [name,File.binread(File.join(source,name))] }
  result=run.call(dir)
  abort 'FAIL: acquisition claims readiness/consistency or missing copies' unless result[:status]=='usb_mutable_captured' && result[:host_files_created]==3 &&
    result[:saved_bytes]==111104 && result[:mutable_copies_verified]==true && result[:writes_performed]==0 && result[:installation_ready]==false &&
    result[:observation]=='recovery_unmounted_two_matching_reads' && result[:filesystem_consistency]=='not_established_by_integrity_check'
  abort 'FAIL: leaked private acquisition receipt' if JSON.generate(result).include?('MUTABLE_TEST_SERIAL') || JSON.generate(result).include?(parent)
  abort 'FAIL: unexpected/unsafe outputs' unless Dir.children(dir).sort==%w[metadata.raw mutable.json udisk.raw] && File.stat(dir).mode&07777==0700
  %w[udisk.raw metadata.raw].each do |name|
    abort 'FAIL: changed bytes/privacy' unless File.binread(File.join(dir,name))==originals.fetch(name) && File.stat(File.join(dir,name)).mode&07777==0600
  end
  originals.each { |name,bytes| abort 'FAIL: acquisition wrote source' unless File.binread(File.join(source,name))==bytes }
  abort 'FAIL: only one mutable pass' unless JSON.parse(File.read(File.join(source,'state.json')))['reads']=={'UDISK'=>2,'metadata'=>2}
  bound=A133Mutable.verify(File.join(dir,'mutable.json'),serial:'MUTABLE_TEST_SERIAL',cid:'0123456789abcdef0123456789abcdef')
  abort 'FAIL: next stage cannot verify output' unless bound['mutable_copies_verified']==true
  checks+=1
  reject=lambda do |reason,dir,&block|
    begin
      block.call; abort 'FAIL: accepted '+reason
    rescue A133UsbBackup::Invalid => error
      abort 'FAIL: expected '+reason+', got '+error.message unless error.message==reason
      abort 'FAIL: private error cause/diagnostic' unless error.cause.nil? && !error.full_message.include?(parent) && !error.full_message.include?('PRIVATE_')
    end
    abort 'FAIL: failed acquisition published manifest' if File.exist?(File.join(dir,'mutable.json')) && reason!='usb_backup_output_changed'
    checks+=1
  end
  cases=[
    ['usb_backup_device_not_ready',{'state'=>'device'}],
    ['usb_root_required',{'root'=>'2000'}],
    ['usb_target_mounted',{'mountinfo'=>"1 0 179:6 / /alias rw - ext4 /dev/block/alias rw\n",'block_ids'=>['179:6']}],
    ['usb_command_failed',{'inventory_unreadable'=>true}],
    ['usb_mount_inventory_invalid',{'mountinfo'=>'invalid'}],
    ['usb_swap_inventory_invalid',{'swaps'=>'invalid'}],
    ['usb_target_in_use',{'swaps'=>"Filename Type Size Used Priority\n/dev/block/alias partition 100 0 1\n"}],
    ['usb_target_in_use',{'holders'=>true}],
    ['usb_mutable_stream_changed',{'mutate'=>'UDISK'}],
    ['usb_mutable_stream_changed',{'mutate'=>'metadata'}],
    ['usb_backup_source_changed',{'drift'=>true}],
    ['usb_target_mounted',{'late_mount'=>true}],
    ['usb_backup_stream_size_mismatch',{'mode'=>'short'}],
    ['usb_backup_stream_size_mismatch',{'mode'=>'oversized'}],
    ['usb_diagnostics',{'mode'=>'noisy'}],
    ['usb_command_failed',{'mode'=>'failed'}]
  ]
  cases.each do |reason,state|
    setup.call(state); dir=destination.call; reject.call(reason,dir) {run.call(dir)}
  end
  setup.call; dir=destination.call
  reject.call('usb_mutable_identity_mismatch',dir) {run.call(dir,cid:'f'*32)}
  setup.call; dir=destination.call
  result=run.call(dir,root_method:'vendor_su'); abort 'FAIL: vendor root read wrapper' unless result[:status]=='usb_mutable_captured'; checks+=1
  [:file,:directory,:post_manifest].each do |kind|
    setup.call; dir=destination.call; $mutable_source_fault={directory:dir,kind:kind}
    reason=kind==:directory ? 'usb_backup_unsafe_directory' : 'usb_backup_output_changed'
    reject.call(reason,dir) {run.call(dir)}
    abort 'FAIL: late host fault did not execute' if $mutable_source_fault || $mutable_publish_fault
    abort 'FAIL: prepublication late mutation published manifest' if kind!=:post_manifest && File.exist?(File.join(dir,'mutable.json'))
    File.chmod(0700,dir)
  end
  [:fsync,:collision].each do |fault|
    setup.call; dir=destination.call; $mutable_fsync_fault=fault
    reject.call(fault==:fsync ? 'usb_backup_host_file_error' : 'usb_backup_output_exists',dir) {run.call(dir)}
    abort 'FAIL: partial evidence discarded/fault did not execute' unless !$mutable_fsync_fault && File.file?(File.join(dir,'udisk.raw.partial'))
    abort 'FAIL: collision replaced' if fault==:collision && File.binread(File.join(dir,'udisk.raw'))!='FOREIGN'
  end
  setup.call; dir=destination.call; Dir.mkdir(dir,0700); File.write(File.join(dir,'sentinel'),'KEEP')
  reject.call('usb_backup_destination_exists',dir) {run.call(dir)}
  abort 'FAIL: existing evidence modified' unless File.read(File.join(dir,'sentinel'))=='KEEP'
  setup.call('mode'=>'stall'); dir=destination.call
  start=Process.clock_gettime(Process::CLOCK_MONOTONIC)
  reject.call('usb_backup_timeout',dir) {run.call(dir,timeout:1)}
  abort 'FAIL: unbounded timeout' unless Process.clock_gettime(Process::CLOCK_MONOTONIC)-start<5
  setup.call; dir=destination.call
  bootstrap=File.join(parent,'cli.rb'); File.write(bootstrap,"require #{File.join(__dir__,'a133-mutable-test-support.rb').inspect}\nMutableFixture.configure\n$0=#{tool.inspect}\nload #{tool.inspect}\n")
  out,err,status=Open3.capture3(RbConfig.ruby,bootstrap,'--adb',adb,'--serial','MUTABLE_TEST_SERIAL','--cid','0123456789abcdef0123456789abcdef',dir)
  abort 'FAIL: CLI failed/leaked' unless status.success? && err.empty? && JSON.parse(out)['status']=='usb_mutable_captured' &&
    !out.include?('MUTABLE_TEST_SERIAL') && !out.include?('0123456789abcdef') && !out.include?(parent)
  checks+=1
  out,err,status=Open3.capture3(RbConfig.ruby,tool,'--write',destination.call)
  abort 'FAIL: write CLI flag' unless !status.success? && err.empty? && JSON.parse(out)['reason']=='invalid_arguments'; checks+=1
end
puts "USB mutable backup: #{checks} actual-dd recovery and failure cases passed"
