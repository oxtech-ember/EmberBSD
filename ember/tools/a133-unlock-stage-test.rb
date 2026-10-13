#!/usr/bin/env ruby
# Origin: EmberBSD - real-process journaled unlock, refusal and ambiguous-resume contracts.
require 'tmpdir'
require 'rbconfig'
require 'open3'
require_relative 'a133-recovery-reboot-test-support'
tool=File.join(__dir__,'a133-unlock-stage.rb')
abort 'FAIL: journaled unlock stage is missing' unless File.file?(tool)
require tool
checks=0
Dir.mktmpdir('a133-unlock-stage-') do |dir|
  context=RebootFixture.create(dir)
  original=context[:original]
  vars=A133Env.decode(original.byteslice(0,131072)).fetch(:entries).to_h
  additions={
    'hook'=>'ember_unlock_flag ember_unlock_next',
    'ember_unlock_flag'=>'pst write fastboot_status_flag unlocked',
    'ember_unlock_next'=>'setenv hook ember_restore_normal ember_recovery_once; run ember_save_env ember_reset',
    'ember_save_env'=>'saveenv','ember_reset'=>'reset',
    'ember_restore_normal'=>'setenv hook; saveenv',
    'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'}
  tail=original.byteslice(131072,16777216-131072)
  armed=RebootFixture.encode(vars.merge(additions))+tail
  consumed=RebootFixture.encode(vars.merge(additions.reject { |key,_| key=='hook' }).to_a.reverse.to_h,true,"\0")+tail
  File.binwrite(File.join(dir,'unlock-consumed.bin'),consumed.byteslice(0,131072))
  File.binwrite(File.join(dir,'saved-original'),original)
  File.write(File.join(dir,'adb-source.rb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-unlock-stage-fixture.rb')))
  mutable={'status'=>'mutable_integrity_verified','serial'=>RebootFixture::SERIAL,'cid'=>UsbFixture::CID,
    'mutable_copies_verified'=>true,'gpt_sha256'=>context[:backup]['gpt_sha256'],
    'observation'=>'recovery_unmounted_two_matching_reads','filesystem_consistency'=>'not_established_by_integrity_check',
    'partition_sha256'=>{'UDISK'=>'a'*64,'metadata'=>'b'*64}}
  sequence=0
  session=nil
  reset=lambda do |state={},live=original|
    sequence+=1; session=File.join(dir,"session-#{sequence}")
    RebootFixture.reset(dir,context,{'state'=>'device','journal_path'=>File.join(session,'state.json')}.merge(state),live)
    %w[write-count reboot-count].each { |name| File.unlink(File.join(dir,name)) if File.exist?(File.join(dir,name)) }
  end
  params=lambda do
    {directory:session,adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,
     original_env:original,backup:context[:backup],mutable:mutable,locked_round_trip_verified:true,timeout:5,wait_timeout:1}
  end
  count=lambda { |name| File.exist?(File.join(dir,name)) ? File.readlines(File.join(dir,name)).size : 0 }
  failed=lambda do |reason,changes={},opts={},live=original,&before|
    reset.call(changes,live)
    before.call if before
    begin
      A133Unlock::Stage.run(**params.call.merge(opts)); abort 'FAIL: unsafe unlock accepted '+reason
    rescue A133Unlock::Stage::Invalid => error
      abort "FAIL: expected #{reason}, got #{error.message}" unless error.message==reason && error.cause.nil?
      abort 'FAIL: private diagnostics' if error.full_message.include?(dir) || error.full_message.include?('PRIVATE_')
    end
    abort 'FAIL: preflight performed an effect' unless count.call('write-count')==0 && count.call('reboot-count')==0
    checks+=1
  end
  reset.call
  result=A133Unlock::Stage.run(**params.call)
  abort 'FAIL: unlock return/readback/order' unless result[:status]=='unlocked_recovery_verified' &&
    result[:installation_ready]==false && result[:possible_write] && result[:possible_reboot] &&
    File.binread(File.join(dir,'part2'))==original && File.readlines(File.join(dir,'write-count')).map(&:strip)==%w[arming restoring] &&
    count.call('reboot-count')==1
  again=A133Unlock::Stage.run(**params.call)
  abort 'FAIL: completed repeat replayed an effect' unless again[:status]=='unlocked_recovery_verified' &&
    count.call('write-count')==2 && count.call('reboot-count')==1
  checks+=2
  failed.call('locked_round_trip_required',{},locked_round_trip_verified:false)
  failed.call('invalid_unlock_mutable',{},mutable:mutable.merge('gpt_sha256'=>'0'*64))
  failed.call('usb_unlock_initial_state',{'locked'=>'0','verified'=>'orange'})
  failed.call('usb_unlock_initial_state',{'state'=>'recovery','locked'=>'0','verified'=>'orange'})
  failed.call('usb_unlock_identity_mismatch',{'cid'=>'f'*32})
  failed.call('usb_unlock_transport_required',{'tcp'=>true})
  failed.call('usb_unlock_duplicate_device',{'duplicate'=>true})
  failed.call('usb_target_in_use',{'holders'=>true})
  failed.call('usb_target_in_use',{'swaps'=>"Filename Type Size Used Priority\n/dev/block/zram0 partition 1 0 -2\n"})
  failed.call('usb_root_required',{'root'=>'2000'})
  failed.call('usb_unlock_environment_unknown',{}, {},original.sub('TAIL_KEEP','TAIL_FAIL'))
  %w[bootloader boot recovery].zip([1,3,6]).each do |role,index|
    failed.call('usb_'+role+'_changed') { File.open(File.join(dir,"part#{index}"),'r+b') { |file| file.write('X') } }
  end
  reset.call({},armed)
  begin
    A133Unlock::Stage.run(**params.call); abort 'FAIL: fresh armed state adopted'
  rescue A133Unlock::Stage::Invalid => error
    abort 'FAIL: fresh armed reason' unless error.message=='usb_unlock_initial_state'
  end
  abort 'FAIL: fresh armed state rebooted' unless count.call('reboot-count')==0
  checks+=1
  ['partial','sync_failed'].each do |mode|
    reset.call('write_mode'=>mode)
    begin
      A133Unlock::Stage.run(**params.call); abort 'FAIL: failed prefix accepted'
    rescue A133Unlock::Stage::Invalid => error
      abort 'FAIL: prefix failure flags' unless error.message=='usb_command_failed' && error.report[:possible_write] && !error.report[:possible_reboot]
    end
    abort 'FAIL: failed prefix rebooted' unless count.call('reboot-count')==0
    state=JSON.parse(File.read(File.join(dir,'state.json'))); state.delete('write_mode'); File.write(File.join(dir,'state.json'),JSON.generate(state))
    if mode=='partial'
      begin
        A133Unlock::Stage.run(**params.call); abort 'FAIL: unknown prefix repaired blindly'
      rescue A133Unlock::Stage::Invalid => error
        abort 'FAIL: partial prefix adopted' unless error.message=='usb_unlock_environment_unknown' && count.call('write-count')==1
      end
    else
      result=A133Unlock::Stage.run(**params.call)
      abort 'FAIL: exact armed resume rewrote prefix' unless result[:status]=='unlocked_recovery_verified' && count.call('write-count')==2 && count.call('reboot-count')==1
    end
    checks+=1
  end
  reset.call('after_reboot'=>{'hardware_sectors'=>16384})
  begin
    A133Unlock::Stage.run(**params.call); abort 'FAIL: hardware geometry drift accepted'
  rescue A133Unlock::Stage::Invalid => error
    abort 'FAIL: hardware drift flags' unless error.message=='unlock_hardware_changed' && error.report[:possible_reboot] && count.call('write-count')==1
  end
  checks+=1
  reset.call('reboot_failure'=>true)
  begin
    A133Unlock::Stage.run(**params.call); abort 'FAIL: reboot failure accepted'
  rescue A133Unlock::Stage::Invalid => error
    abort 'FAIL: lost persistent flags' unless error.message=='usb_command_failed' && error.report[:possible_write] && error.report[:possible_reboot]
  end
  state=JSON.parse(File.read(File.join(dir,'state.json'))); state.delete('reboot_failure'); File.write(File.join(dir,'state.json'),JSON.generate(state))
  result=A133Unlock::Stage.run(**params.call)
  abort 'FAIL: failed reboot replayed on resume' unless result[:status]=='unlocked_recovery_verified' && count.call('reboot-count')==1 && File.binread(File.join(dir,'part2'))==original
  checks+=1
  reset.call
  child=<<~RUBY
    require #{tool.inspect}
    A133Usb::Channel.prepend(Module.new do
      def run(arguments,**options,&block)
        Process.exit!(77) if arguments.last=='reboot'
        super
      end
    end)
    options=JSON.parse(ARGV.fetch(0)).transform_keys(&:to_sym)
    options[:original_env]=File.binread(options.delete(:original_path))
    A133Unlock::Stage.run(**options)
  RUBY
  opts=params.call.reject { |key,_| key==:original_env }.merge(original_path:File.join(dir,'saved-original'))
  out,err,status=Open3.capture3(RbConfig.ruby,'-e',child,JSON.generate(opts))
  abort 'FAIL: interrupted reboot fixture' unless status.exitstatus==77 && out.empty? && err.empty? &&
    count.call('reboot-count')==0 && JSON.parse(File.read(File.join(session,'state.json')))['possible_reboot']==true
  begin
    A133Unlock::Stage.run(**params.call); abort 'FAIL: ambiguous intent replayed'
  rescue A133Unlock::Stage::Invalid => error
    abort 'FAIL: ambiguous reboot boundary' unless error.message=='usb_unlock_wait_timeout' && count.call('reboot-count')==0 && count.call('write-count')==1
  end
  checks+=1
  reset.call
  changing=Marshal.load(Marshal.dump(params.call))
  worker=Thread.new do
    Thread.current.report_on_exception=false
    sleep 0.001 until File.exist?(File.join(dir,'observed'))
    changing[:serial].replace('FOREIGN_SERIAL')
    changing[:cid].replace('f'*32)
    changing[:directory].replace(File.join(dir,'FOREIGN_DIRECTORY'))
    changing[:adb].replace('/unavailable/PRIVATE_LAUNCHER')
    changing[:original_env].setbyte(16777215,0x55)
    changing[:backup]['partition_sha256']['env'].replace('0'*64)
    changing[:mutable]['partition_sha256']['UDISK'].replace('0'*64)
  end
  result=A133Unlock::Stage.run(**changing)
  worker.value
  abort 'FAIL: caller mutation altered pinned unlock context' unless result[:status]=='unlocked_recovery_verified' &&
    File.binread(File.join(dir,'part2'))==original && count.call('reboot-count')==1
  checks+=1
end
puts "Unlock stage: #{checks} real-I/O transition/refusal/resume cases passed"
