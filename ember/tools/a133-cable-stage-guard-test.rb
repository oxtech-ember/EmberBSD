#!/usr/bin/env ruby
# Origin: EmberBSD - native handoff and post-image policy drift regressions.
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
  CablePublicationFault.root=session; CablePublicationFault.phase='protecting'
  stopped.call('cable_journal_io_failed') { A133Cable.run(**options.call) }
  state_path=File.join(dir,'state.json'); clean=JSON.parse(File.read(state_path))
  failures=[]
  cases={'protection_hardware'=>'cable_hardware_changed','image_hardware'=>'cable_hardware_changed',
    'holders'=>'usb_target_in_use','tail'=>'usb_environment_unknown','swaps'=>'usb_target_in_use',
    'thread_mount'=>'usb_target_mounted','after_root'=>'usb_target_in_use','gpt'=>'cable_identity_changed'}
  selected=ARGV.empty? ? cases : cases.select { |key,_| ARGV.include?(key) }
  abort 'FAIL: unknown guard case' if selected.empty?
  injection=lambda do |point|
    next unless @mode && (point==:protection ? @mode=='protection_hardware' : @mode!='protection_hardware')
    state=JSON.parse(File.read(state_path))
    case @mode
    when 'protection_hardware','image_hardware' then state['hardware_sectors']=16384
    when 'holders' then state['holders']=true
    when 'tail' then File.open(File.join(dir,'part2'),'r+b') { |f| f.seek(131072); f.write('DRIFT') }
    when 'swaps' then state['swaps']="Filename\tType\tSize\tUsed\tPriority\n/dev/block/mmcblk0p17 partition 1024 0 -2\n"
    when 'thread_mount'
      state['thread_mountinfo']="2 1 179:17 / /hidden rw - ext4 /dev/block/mmcblk0p17 rw\n"
      state['block_ids']=['179:17']
    when 'after_root' then state['after_root']=true
    when 'gpt'
      File.open(File.join(dir,'disk'),'r+b') do |file|
        [512,31037849600-512].each do |offset|
          file.seek(offset); header=file.read(512)
          header[56,16]='Z'*16; header[16,4]=[0].pack('V')
          header[16,4]=[Zlib.crc32(header.byteslice(0,92))].pack('V')
          file.seek(offset); file.write(header)
        end
      end
    end
    state['mode']='partial' unless @mode=='after_root'
    File.write(state_path,JSON.generate(state))
  end
  A133Recovery::Protection.prepend(Module.new do
    define_method(:install) { |**params| injection.call(:protection); super(**params) }
  end)
  A133Install.singleton_class.prepend(Module.new do
    define_method(:run) { |**params| injection.call(:images); super(**params) }
  end)
  selected.each do |mode,reason|
    @mode=mode
    File.write(state_path,JSON.generate(clean))
    File.binwrite(File.join(dir,'part2'),original)
    File.open(File.join(dir,'part17'),'r+b') { |f| f.write('CHANGED_ROOT') }
    before=effects.call
    begin
      A133Cable.run(**options.call)
      failures << mode+': returned success'
    rescue A133Cable::Invalid => error
      failures << mode+': wrong reason '+error.message unless error.message==reason && error.cause.nil?
    end
    images=(effects.call.drop(before.size)).select { |effect| effect.start_with?('mmcblk0p') }
    want=mode=='after_root' ? ['mmcblk0p17'] : []
    failures << mode+': unsafe image effects '+images.inspect unless images==want
    checks+=1
  end
  abort failures.join("\n") unless failures.empty?
end
puts "Cable guard regressions: #{checks-1} native handoff and post-image drift cases passed"
