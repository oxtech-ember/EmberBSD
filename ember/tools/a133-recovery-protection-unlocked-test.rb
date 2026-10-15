#!/usr/bin/env ruby
# Origin: EmberBSD - opt-in unlocked-only protection admission without device effects.
require 'tmpdir'
require_relative 'a133-recovery-protection'
require_relative 'a133-recovery-reboot-test-support'
checks=0
Dir.mktmpdir('a133-protection-unlocked-') do |dir|
  context=RebootFixture.create(dir)
  mutable={'serial'=>RebootFixture::SERIAL,'cid'=>UsbFixture::CID,'status'=>'mutable_integrity_verified',
    'mutable_copies_verified'=>true,'gpt_sha256'=>context[:backup]['gpt_sha256'],
    'observation'=>'recovery_unmounted_two_matching_reads','filesystem_consistency'=>'not_established_by_integrity_check',
    'partition_sha256'=>{'UDISK'=>'a'*64,'metadata'=>'b'*64}}
  options={adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,timeout:5}
  [[true,'usb_protection_state_unsupported'],['yes','invalid_protection_options']].each do |flag,reason|
    begin
      A133Recovery::Protection.new(**options.merge(require_unlocked:flag)).install(original_env:context[:original],
        backup:context[:backup],mutable:mutable)
      abort 'FAIL: locked recovery accepted by composed protection'
    rescue A133Usb::Invalid => error
      abort 'FAIL: unexpected protection refusal' unless error.message==reason && error.write_attempted==false
    rescue ArgumentError
      abort 'FAIL: unlocked-only protection gate missing'
    end
    abort 'FAIL: protection refusal wrote' if File.exist?(File.join(dir,'written'))
    checks+=1
  end
end
puts "Unlocked-only protection: #{checks} native admission cases passed"
