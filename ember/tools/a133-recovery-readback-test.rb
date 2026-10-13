#!/usr/bin/env ruby
# Origin: EmberBSD - immutable policy and zero-write whole factory-state verification.
require 'tmpdir'
require_relative 'a133-recovery-reboot-test-support'
%w[a133-recovery-policy.rb a133-recovery-readback.rb].each { |name| abort 'FAIL: policy/readback missing' unless File.file?(File.join(__dir__,name)) }
require_relative 'a133-recovery-readback'
checks=0
Dir.mktmpdir('a133-readback-') do |dir|
  context=RebootFixture.create(dir)
  policy=lambda { A133Recovery::Policy.new(serial:RebootFixture::SERIAL,cid:UsbFixture::CID,original_env:context[:original],backup:context[:backup]) }
  verify=lambda do |expected,state='device',p=policy.call,options={}|
    A133Recovery::Readback.new(adb:File.join(dir,'adb'),policy:p,state:state,
      root_method:state=='device' ? 'vendor_su' : 'adbd',**options).verify(expected:expected)
  end
  failed=lambda do |reason,expected,state='device',p=policy.call,options={}|
    before=Digest::SHA256.file(File.join(dir,'part2')).hexdigest
    begin
      verify.call(expected,state,p,options); abort 'FAIL: unsafe readback accepted '+reason
    rescue A133Usb::Invalid => error
      abort "FAIL: expected #{reason}, got #{error.message}" unless error.message==reason && error.write_attempted==false && error.cause.nil? &&
        !error.full_message.include?(dir) && !error.full_message.include?('PRIVATE_')
    end
    abort 'FAIL: readback wrote env' unless Digest::SHA256.file(File.join(dir,'part2')).hexdigest==before && !File.exist?(File.join(dir,'written'))
    checks+=1
  end
  [['original','device',:original],['original','recovery',:original],['armed','device',:armed],['consumed','recovery',:consumed],['consumed','recovery',:reordered]].each do |expected,state,bytes|
    RebootFixture.reset(dir,context,{'state'=>state},context[bytes])
    result=verify.call(expected,state)
    abort 'FAIL: verified receipt/state/write boundary' unless result[:status]=='environment_state_verified' && result[:state]==state && result[:expected]==expected &&
      result[:env_sha256]==Digest::SHA256.file(File.join(dir,'part2')).hexdigest && result[:hardware_bytes]==4194304 &&
      result[:writes_performed]==0 && result[:installation_ready]==false && !File.exist?(File.join(dir,'written'))
    checks+=1
  end
  RebootFixture.reset(dir,context,{'state'=>'device'})
  failed.call('usb_environment_unexpected','armed')
  failed.call('invalid_environment_expectation','consumed')
  failed.call('invalid_environment_expectation','unknown')
  RebootFixture.reset(dir,context,{},context[:altered]); failed.call('usb_environment_unexpected','consumed','recovery')
  RebootFixture.reset(dir,context,{},context[:consumed].sub('TAIL_KEEP','TAIL_FAIL')); failed.call('usb_environment_unexpected','consumed','recovery')
  %w[bootloader boot recovery].zip([1,3,6]).each do |role,index|
    RebootFixture.reset(dir,context,{'state'=>'device'})
    File.open(File.join(dir,"part#{index}"),'r+b') { |f| f.write('X') }
    failed.call('usb_'+role+'_changed','original')
  end
  [
    [{'cid'=>'f'*32},'usb_readback_identity_mismatch'],
    [{'locked'=>'0','verified'=>'orange'},'usb_readback_state_unsupported'],
    [{'holders'=>true},'usb_target_in_use'],
    [{'holders_unreadable'=>true},'usb_command_failed'],
    [{'read_mode'=>'short'},'usb_backup_stream_size_mismatch']
  ].each do |state,reason|
    RebootFixture.reset(dir,context,{'state'=>'device'}.merge(state)); failed.call(reason,'original')
  end
  b=Marshal.load(Marshal.dump(context[:backup])); env=context[:original].dup
  p=A133Recovery::Policy.new(serial:RebootFixture::SERIAL,cid:UsbFixture::CID,original_env:env,backup:b)
  abort 'FAIL: policy not deeply immutable' unless p.frozen? && p.data.frozen? && p.data.values.all? { |v| !v.is_a?(String) || v.frozen? } &&
    p.data[:consumed].frozen? && p.data[:consumed].all? { |k,v| k.frozen? && v.frozen? }
  b['partition_sha256'].values.each { |v| v.replace('0'*64) }; b['gpt_sha256'].replace('0'*64); env.setbyte(16777215,0)
  RebootFixture.reset(dir,context,{'state'=>'device'})
  verify.call('original','device',p); checks+=1
  b=Marshal.load(Marshal.dump(context[:backup])); b['partition_sha256'][:extra]='a'*64
  begin
    A133Recovery::Policy.new(serial:RebootFixture::SERIAL,cid:UsbFixture::CID,original_env:context[:original],backup:b)
    abort 'FAIL: malformed policy accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: policy reason leaked' unless error.message=='invalid_entry_backup'
  end
  checks+=1
  RebootFixture.reset(dir,context,{'state'=>'device'})
  file=File.join(dir,'launch'); File.write(file,'PRIVATE_HOST_LOCATION')
  failed.call('usb_readback_io_failed','original','device',policy.call,{adb:File.join(file,'adb')})
end
puts "Recovery readback: #{checks} immutable/actual-I/O cases passed"
