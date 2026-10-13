#!/usr/bin/env ruby
# Origin: EmberBSD - caller expectation mutation cannot change USB verification or its receipt.
require 'tmpdir'
require_relative 'a133-recovery-reboot-test-support'
require_relative 'a133-recovery-readback'
checks=0
Dir.mktmpdir('a133-readback-expectation-') do |dir|
  context=RebootFixture.create(dir)
  policy=A133Recovery::Policy.new(serial:RebootFixture::SERIAL,cid:UsbFixture::CID,
    original_env:context[:original],backup:context[:backup])
  readback=lambda { A133Recovery::Readback.new(adb:File.join(dir,'adb'),policy:policy,state:'device',root_method:'vendor_su') }
  unless ARGV==['--receipt']
    RebootFixture.reset(dir,context,{'state'=>'device'})
    expected='armed'; mutated=false
    worker=Thread.new do
      sleep 0.005 until File.exist?(File.join(dir,'observed'))
      expected.replace('original'); mutated=true
    end
    begin
      readback.call.verify(expected:expected)
      abort 'FAIL: caller mutation bypassed the originally required armed state'
    rescue A133Usb::Invalid=>error
      abort 'FAIL: pinned expectation refusal/flags/cause' unless error.message=='usb_environment_unexpected' &&
        error.write_attempted==false && error.cause.nil? && !error.full_message.include?(dir)
    ensure
      worker.kill; worker.join
    end
    abort 'FAIL: expectation race or storage boundary not exercised' unless mutated && expected=='original' &&
      !File.exist?(File.join(dir,'written')) && File.binread(File.join(dir,'part2'))==context[:original]
    checks+=1
  end
  RebootFixture.reset(dir,context,{'state'=>'device'})
  expected='original'
  receipt=readback.call.verify(expected:expected)
  expected.replace('consumed')
  abort 'FAIL: returned expectation changed with the caller argument' unless receipt[:expected]=='original' && receipt[:expected].frozen? &&
    receipt[:status]=='environment_state_verified' && receipt[:writes_performed]==0 && !File.exist?(File.join(dir,'written'))
  checks+=1
end
puts "Recovery readback expectation: #{checks} live/post-return mutation regressions passed"
