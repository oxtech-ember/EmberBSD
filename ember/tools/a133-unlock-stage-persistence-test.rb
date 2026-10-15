#!/usr/bin/env ruby
# Origin: EmberBSD - actual-I/O refusal to replay after post-rename fsync failure.
require 'tmpdir'
require_relative 'a133-recovery-reboot-test-support'
require_relative 'a133-unlock-stage'
module UnlockStageFsyncFault
  class << self; attr_accessor :directory,:injected,:mode; end
  def fsync
    value=super
    if stat.directory? && path==UnlockStageFsyncFault.directory && !UnlockStageFsyncFault.injected
      data=JSON.parse(File.read(File.join(path,'state.json')))
      if data['phase']=='rebooting'
        UnlockStageFsyncFault.injected=true
        if UnlockStageFsyncFault.mode=='scope'
          File.rename(path,path+'.moved')
          A133Unlock::Journal.open(path,data['context']) { }
        else
          raise Errno::EIO,'PRIVATE_STAGE_FSYNC'
        end
      end
    end
    value
  end
end
File.prepend(UnlockStageFsyncFault)
modes=ARGV.empty? ? %w[fsync scope] : ARGV
modes.each do |mode|
 Dir.mktmpdir('a133-unlock-persistence-') do |dir|
  fixture=RebootFixture.create(dir)
  session=File.join(dir,'session')
  RebootFixture.reset(dir,fixture,{'state'=>'device','journal_path'=>File.join(session,'state.json')})
  File.write(File.join(dir,'adb-source.rb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-unlock-stage-fixture.rb')))
  mutable={'status'=>'mutable_integrity_verified','serial'=>RebootFixture::SERIAL,'cid'=>UsbFixture::CID,
    'mutable_copies_verified'=>true,'gpt_sha256'=>fixture[:backup]['gpt_sha256'],
    'observation'=>'recovery_unmounted_two_matching_reads','filesystem_consistency'=>'not_established_by_integrity_check',
    'partition_sha256'=>{'UDISK'=>'a'*64,'metadata'=>'b'*64}}
  options={directory:session,adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,
    original_env:fixture[:original],backup:fixture[:backup],mutable:mutable,locked_round_trip_verified:true,
    timeout:5,wait_timeout:1}
  UnlockStageFsyncFault.directory=session
  UnlockStageFsyncFault.injected=false
  UnlockStageFsyncFault.mode=mode
  reasons=mode=='scope' ? ['unlock_journal_scope_changed'] : %w[unlock_journal_io_failed usb_unlock_wait_timeout]
  reasons.each do |expected|
    begin
      A133Unlock::Stage.run(**options)
      abort 'FAIL: persistence ambiguity accepted'
    rescue A133Unlock::Stage::Invalid => error
      abort 'FAIL: persistence reason or intent' unless error.message==expected && error.cause.nil? &&
        error.report[:possible_reboot] && error.report[:possible_write]
      abort 'FAIL: private persistence diagnostic' if error.full_message.include?(dir) || error.full_message.include?('PRIVATE_')
    end
    recorded=mode=='scope' ? session+'.moved' : session
    data=JSON.parse(File.read(File.join(recorded,'state.json')))
    abort 'FAIL: Stage rescue erased published reboot marker' unless data['possible_reboot'] && data['possible_write']
    if mode=='scope'
      replacement=JSON.parse(File.read(File.join(session,'state.json')))
      abort 'FAIL: detached Store changed replacement journal' unless replacement['phase']=='checking' &&
        replacement['revision']==0 && !replacement['possible_write'] && !replacement['possible_reboot']
      abort 'FAIL: detached journal effects not conservative' unless error.report[:effects_unknown]
    end
    abort 'FAIL: persistence failure replayed a device effect' unless
      File.readlines(File.join(dir,'write-count')).map(&:strip)==['arming'] && !File.exist?(File.join(dir,'reboot-count'))
  end
 end
end
puts "Unlock persistence: #{modes.size} post-rename failure/scope scenarios submitted no reboot"
