#!/usr/bin/env ruby
# Origin: EmberBSD - admission failure must not erase possible prior session effects.
require 'tmpdir'
require_relative 'a133-unlock-stage'
context={'serial'=>'ADMISSION_ONLY','cid'=>'0'*32}
%w[backup gpt bootloader boot recovery original_env unlock_env mutable].each { |key| context[key+'_sha256']='a'*64 }
Dir.mktmpdir('a133-unlock-admission-') do |dir|
  session=File.join(dir,'session')
  A133Unlock::Journal.open(session,context) { |store| store.record('rebooting',write:true,reboot:true) }
  options={directory:session,adb:'/unavailable/PRIVATE_ADB',serial:'ADMISSION_ONLY',cid:'0'*32,
    original_env:'',backup:{},mutable:{},locked_round_trip_verified:true}
  [{locked_round_trip_verified:false},{wait_timeout:0}].each do |change|
    begin
      A133Unlock::Stage.run(**options.merge(change)); abort 'FAIL: invalid admission accepted'
    rescue A133Unlock::Stage::Invalid => error
      abort 'FAIL: admission reported no possible prior effects' unless error.report[:effects_unknown]==true &&
        error.report[:possible_write]==true && error.report[:possible_reboot]==true && error.cause.nil?
    end
  end
end
puts 'Unlock admission: 2 prior-effects refusal cases passed'
