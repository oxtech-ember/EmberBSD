#!/usr/bin/env ruby
# Origin: EmberBSD - private composed-stage intent, validation and lock tests.
require 'tmpdir'
require 'open3'
require 'rbconfig'
tool=File.join(__dir__,'a133-cable-journal.rb')
abort 'FAIL: cable journal missing' unless File.file?(tool)
require tool
context={'serial'=>'CABLE_TEST','cid'=>'0'*32}
%w[backup gpt bootloader boot recovery original_env unlock_env mutable protected_env bundle].each { |key| context[key+'_sha256']='a'*64 }
checks=0
failure=lambda do |reason,&block|
  begin
    block.call; abort 'FAIL: accepted '+reason
  rescue A133Cable::Invalid => error
    abort 'FAIL: wrong bounded reason' unless error.message==reason && error.cause.nil?
  end
  checks+=1
end
Dir.mktmpdir('a133-cable-journal-') do |base|
  dir=File.join(base,'session'); escaped=nil
  A133Cable::Journal.open(dir,context) do |store|
    escaped=store
    failure.call('cable_journal_state_invalid') { store.record('protecting',protection:true) }
    store.record('unlocking',write:true,reboot:true)
    store.record('unlocked',unlocked:true,hardware_bytes:4194304)
    store.record('protecting',protection:true)
    store.record('failed',reason:'usb_timeout')
    report=store.report
    abort 'FAIL: lost protection boundary' unless report[:unlock_verified] && report[:protection_started] &&
      report[:possible_reboot] && report[:possible_write] && report[:installation_ready]==false
    failure.call('cable_journal_busy') { A133Cable::Journal.open(dir,context) { } }
    snapshot=store.snapshot; snapshot['protection_started']=false
    abort 'FAIL: mutable snapshot' unless store.report[:protection_started]
    checks+=1
  end
  failure.call('cable_journal_not_locked') { escaped.record('verified') }
  failure.call('cable_journal_context_changed') { A133Cable::Journal.open(dir,context.merge('bundle_sha256'=>'f'*64)) { } }
  failure.call('cable_journal_missing') { A133Cable::Journal.open(File.join(base,'missing'),nil) { } }
  saved=File.binread(File.join(dir,'state.json'))
  [saved.sub('"phase":','"phase":"failed","phase":'),saved.sub('"revision":4','"revision":5'),'x'*65537].each do |bad|
    File.binwrite(File.join(dir,'state.json'),bad)
    failure.call('cable_journal_state_invalid') { A133Cable::Journal.open(dir,nil) { } }
  end
  File.binwrite(File.join(dir,'state.json'),saved)
  %w[.lock state.json].each do |name|
    path=File.join(dir,name); File.chmod(0644,path)
    failure.call('cable_journal_file_unsafe') { A133Cable::Journal.open(dir,nil) { } }
    File.chmod(0600,path); File.link(path,path+'.hard')
    failure.call('cable_journal_file_unsafe') { A133Cable::Journal.open(dir,nil) { } }
    File.unlink(path+'.hard')
  end
  File.symlink(dir,File.join(base,'link'))
  failure.call('cable_journal_directory_unsafe') { A133Cable::Journal.open(File.join(base,'link'),context) { } }
  A133Cable::Journal.open(dir,nil) do |store|
    File.rename(File.join(dir,'.lock'),File.join(dir,'.lock.moved'))
    A133Cable::Journal.open(dir,context) do |replacement|
      before=File.binread(File.join(dir,'state.json'))
      failure.call('cable_journal_scope_changed') { store.record('verified') }
      abort 'FAIL: old scope changed replacement' unless File.binread(File.join(dir,'state.json'))==before
    end
  end
end
puts "Cable journal: #{checks} real-file persistence, boundary and refusal cases passed"
