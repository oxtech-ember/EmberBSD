#!/usr/bin/env ruby
# Origin: EmberBSD - native full composition and interrupted preparation/image resume checks.
require 'tmpdir'
require 'open3'
require 'rbconfig'
require_relative 'a133-recovery-reboot-test-support'
tool=File.join(__dir__,'a133-cable-install.rb')
abort 'FAIL: composed cable stage missing' unless File.file?(tool)
require tool
checks=0
module CablePublicationFault
  class << self; attr_accessor :phase,:root; end
  def fsync
    value=super
    if CablePublicationFault.phase && stat.directory? && path==CablePublicationFault.root
      data=JSON.parse(File.read(File.join(path,'state.json')))
      if data['phase']==CablePublicationFault.phase
        CablePublicationFault.phase=nil
        raise Errno::EIO,'PRIVATE_COMPOSED_FSYNC'
      end
    end
    value
  end
end
File.prepend(CablePublicationFault)
Dir.mktmpdir('a133-cable-stage-') do |dir|
  factory=RebootFixture.create(dir)
  original=factory[:original]
  vars=A133Env.decode(original.byteslice(0,131072)).fetch(:entries).to_h
  additions={'hook'=>'ember_unlock_flag ember_unlock_next','ember_unlock_flag'=>'pst write fastboot_status_flag unlocked',
    'ember_unlock_next'=>'setenv hook ember_restore_normal ember_recovery_once; run ember_save_env ember_reset',
    'ember_save_env'=>'saveenv','ember_reset'=>'reset','ember_restore_normal'=>'setenv hook; saveenv',
    'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'}
  File.binwrite(File.join(dir,'unlock-consumed.bin'),RebootFixture.encode(vars.merge(additions.reject { |key,_| key=='hook' })))
  File.write(File.join(dir,'adb-source.rb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  File.write(File.join(dir,'adb-writer.rb'),File.read(File.join(__dir__,'a133-usb-transfer-fixture.rb')))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-cable-install-fixture.rb')))
  mutable={'status'=>'mutable_integrity_verified','serial'=>RebootFixture::SERIAL,'cid'=>UsbFixture::CID,
    'mutable_copies_verified'=>true,'gpt_sha256'=>factory[:backup]['gpt_sha256'],
    'observation'=>'recovery_unmounted_two_matching_reads','filesystem_consistency'=>'not_established_by_integrity_check',
    'partition_sha256'=>{'UDISK'=>'a'*64,'metadata'=>'b'*64}}
  artifacts=%w[root boot resources].map do |role|
    name=role=='root' ? 'source' : role+'.bin'
    File.open(File.join(dir,name),'wb') { |f| f.truncate(33554432); f.write(role) } unless role=='root'
    {'role'=>role,'file'=>name,'bytes'=>File.size(File.join(dir,name)),
      'sha256'=>Digest::SHA256.file(File.join(dir,name)).hexdigest}
  end
  manifest=File.join(dir,'bundle.json')
  document={'schema'=>1,'board'=>'ys-m33-a133','source_commit'=>'a'*40,'artifacts'=>artifacts}
  File.write(manifest,JSON.generate(document))
  session=nil; sequence=0
  reset=lambda do |extra={}|
    sequence+=1; session=File.join(dir,"session-#{sequence}")
    RebootFixture.reset(dir,factory,{'state'=>'device','locked'=>'1','verified'=>'green',
      'outer_path'=>File.join(session,'state.json'),'unlock_path'=>File.join(session,'unlock','state.json'),
      'images_path'=>File.join(session,'images','state.json')}.merge(extra))
    File.open(File.join(dir,'part17'),'r+b') { |f| f.write('CHANGED_ROOT') }
    File.unlink(File.join(dir,'effects')) if File.exist?(File.join(dir,'effects'))
  end
  options=lambda do
    {directory:session,manifest:manifest,adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,
      original_env:original,backup:factory[:backup],mutable:mutable,locked_round_trip_verified:true,timeout:5,wait_timeout:1}
  end
  effects=lambda { File.exist?(File.join(dir,'effects')) ? File.readlines(File.join(dir,'effects'),chomp:true) : [] }
  stopped=lambda do |reason,&body|
    begin
      body.call; abort 'FAIL: accepted '+reason
    rescue A133Cable::Invalid => error
      abort "FAIL: expected #{reason}, got #{error.message}" unless error.message==reason && error.cause.nil?
      abort 'FAIL: private diagnostics leaked' if error.full_message.include?(dir) || error.full_message.include?('PRIVATE_')
      abort 'FAIL: failure claimed installation' unless error.report[:installation_ready]==false
    end
    checks+=1
  end
  verify_bytes=lambda do
    artifacts.each do |row|
      index={'root'=>17,'boot'=>3,'resources'=>1}.fetch(row['role'])
      digest=File.open(File.join(dir,"part#{index}"),'rb') { |f| Digest::SHA256.hexdigest(f.read(row['bytes'])) }
      abort 'FAIL: composed actual image bytes' unless digest==row['sha256']
    end
    env=File.binread(File.join(dir,'part2'))
    abort 'FAIL: env tail damaged' unless env.byteslice(131072,16777216-131072)==original.byteslice(131072,16777216-131072)
    abort 'FAIL: recovery damaged' unless Digest::SHA256.file(File.join(dir,'part6')).hexdigest==factory[:backup]['partition_sha256']['recovery']
    abort 'FAIL: root tail damaged' unless File.open(File.join(dir,'part17'),'rb') { |f| f.seek(2048); f.read(14) }=='TAIL_PRESERVED'
  end
  reset.call
  result=A133Cable.run(**options.call)
  abort 'FAIL: composed success' unless result[:status]=='cable_write_stage_verified' && result[:installation_ready]==false && result[:image_writes_performed]==3
  abort 'FAIL: actual stage order' unless effects.call==%w[unlock_env reboot unlock_env protection mmcblk0p17 mmcblk0p3 mmcblk0p1]
  verify_bytes.call; checks+=1
  before=effects.call
  result=A133Cable.run(**options.call)
  abort 'FAIL: verified repeat performed effects' unless result[:image_writes_performed]==0 && effects.call==before
  checks+=1
  File.open(File.join(dir,'part17'),'r+b') { |f| f.write('DRIFT') }
  File.open(File.join(dir,'part3'),'r+b') { |f| f.write('DRIFT') }
  result=A133Cable.run(**options.call)
  abort 'FAIL: image resume re-entered unlock' unless result[:image_writes_performed]==2 && effects.call==before+%w[mmcblk0p17 mmcblk0p3]
  verify_bytes.call; checks+=1
  # Same semantic bundle in another row order is still the same release.
  File.write(manifest,JSON.generate(document.merge('artifacts'=>artifacts.reverse)))
  result=A133Cable.run(**options.call)
  abort 'FAIL: order changed release context' unless result[:image_writes_performed]==0
  checks+=1
  File.write(manifest,JSON.generate(document.merge('source_commit'=>'b'*40)))
  before=effects.call
  stopped.call('cable_journal_context_changed') { A133Cable.run(**options.call) }
  abort 'FAIL: changed release touched device' unless effects.call==before
  File.write(manifest,JSON.generate(document))
  %w[protecting protected].each do |phase|
    reset.call
    CablePublicationFault.root=session; CablePublicationFault.phase=phase
    stopped.call('cable_journal_io_failed') { A133Cable.run(**options.call) }
    before=effects.call
    result=A133Cable.run(**options.call)
    abort 'FAIL: publication resume replayed unlock' unless effects.call.count('reboot')==1 && effects.call.count('unlock_env')==2 &&
      result[:status]=='cable_write_stage_verified' && effects.call.count('protection')==1
    verify_bytes.call; checks+=1
  end
  reset.call
  CablePublicationFault.root=session; CablePublicationFault.phase='protecting'
  stopped.call('cable_journal_io_failed') { A133Cable.run(**options.call) }
  File.open(File.join(dir,'part2'),'r+b') { |f| f.write('UNKNOWN') }
  before=effects.call
  stopped.call('usb_environment_unknown') { A133Cable.run(**options.call) }
  abort 'FAIL: unknown partial environment repaired' unless effects.call==before
  state=JSON.parse(File.read(File.join(dir,'state.json')))
  File.write(File.join(dir,'state.json'),JSON.generate(state.merge('hardware_sectors'=>16384)))
  stopped.call('cable_hardware_changed') { A133Cable.run(**options.call) }
  abort 'FAIL: hardware drift wrote' unless effects.call==before
  reset.call
  stopped.call('locked_round_trip_required') { A133Cable.run(**options.call.merge(locked_round_trip_verified:false)) }
  abort 'FAIL: gate refusal observed USB' unless effects.call.empty? && !File.exist?(File.join(dir,'observed'))
  reset.call
  File.write(manifest,JSON.generate(document.merge('source_commit'=>'invalid')))
  stopped.call('invalid_source_commit') { A133Cable.run(**options.call) }
  abort 'FAIL: release admission performed effects' unless effects.call.empty? && !File.exist?(File.join(dir,'observed'))
end
puts "Cable composition: #{checks} native composition, interrupted preparation and resume cases passed"
