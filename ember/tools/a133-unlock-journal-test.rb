#!/usr/bin/env ruby
# Origin: EmberBSD - private unlock journal persistence, exclusion and admission contracts.
require 'tmpdir'
require 'rbconfig'
require 'open3'
require 'json'
tool = File.join(__dir__, 'a133-unlock-journal.rb')
abort 'FAIL: unlock journal is missing' unless File.file?(tool)
require tool
context = {'serial'=>'UNLOCK_TEST_SERIAL','cid'=>'0'*32}
%w[backup gpt bootloader boot recovery original_env unlock_env mutable].each { |key| context[key+'_sha256']='a'*64 }
checks=0
failed=lambda do |reason,&body|
  begin
    body.call
    abort 'FAIL: journal accepted '+reason
  rescue A133Unlock::Invalid => error
    abort 'FAIL: unexpected journal reason '+error.message unless error.message==reason && error.cause.nil?
  end
  checks+=1
end
Dir.mktmpdir('a133-unlock-journal-') do |base|
  dir=File.join(base,'session')
  escaped=nil
  A133Unlock::Journal.open(dir,context) do |store|
    escaped=store
    abort 'FAIL: initial intent' unless store.report[:possible_write]==false && store.report[:possible_reboot]==false
    store.record('arming',write:true,hardware_bytes:4194304)
    store.record('rebooting',reboot:true)
    store.record('failed',reason:'usb_timeout')
    abort 'FAIL: monotonic markers lost' unless store.report[:possible_write] && store.report[:possible_reboot]
    data=JSON.parse(File.read(File.join(dir,'state.json')))
    abort 'FAIL: private snapshot not persisted' unless data['phase']=='failed' && data['revision']==3 && data['hardware_bytes']==4194304
    failed.call('unlock_journal_busy') { A133Unlock::Journal.open(dir,context) { abort 'FAIL: second lock admitted' } }
    failed.call('unlock_journal_not_locked') do
      Thread.new { Thread.current.report_on_exception=false; store.record('verified') }.value
    end
    checks+=1
  end
  failed.call('unlock_journal_not_locked') { escaped.record('verified') }
  A133Unlock::Journal.open(dir,nil) do |store|
    report=store.report
    abort 'FAIL: inspection lost persisted effects' unless report[:possible_write] && report[:possible_reboot] && report[:phase]=='failed'
    abort 'FAIL: report leaked identity' if JSON.generate(report).include?(context['serial'])
    snap=store.snapshot; snap['possible_reboot']=false
    abort 'FAIL: caller changed snapshot' unless store.report[:possible_reboot]
    checks+=1
  end
  failed.call('unlock_journal_context_changed') { A133Unlock::Journal.open(dir,context.merge('cid'=>'f'*32)) { } }
  failed.call('unlock_journal_context_invalid') { A133Unlock::Journal.open(File.join(base,'bad'),context.merge('extra'=>true)) { } }
  failed.call('unlock_journal_missing') { A133Unlock::Journal.open(File.join(base,'absent'),nil) { } }
  %w[state.json .lock].each do |name|
    path=File.join(dir,name)
    File.chmod(0644,path)
    failed.call('unlock_journal_file_unsafe') { A133Unlock::Journal.open(dir,context) { } }
    File.chmod(0600,path)
    File.link(path,path+'.hard')
    failed.call('unlock_journal_file_unsafe') { A133Unlock::Journal.open(dir,context) { } }
    File.unlink(path+'.hard')
  end
  File.chmod(0755,dir)
  failed.call('unlock_journal_directory_unsafe') { A133Unlock::Journal.open(dir,context) { } }
  File.chmod(0700,dir)
  File.symlink(dir,File.join(base,'link'))
  failed.call('unlock_journal_directory_unsafe') { A133Unlock::Journal.open(File.join(base,'link'),context) { } }
  state_path=File.join(dir,'state.json')
  original=File.binread(state_path)
  [original.sub('"phase":','"phase":"failed","phase":'), original.sub('"revision":3','"revision":4'), 'x'*65537].each do |bad|
    File.binwrite(state_path,bad)
    failed.call('unlock_journal_state_invalid') { A133Unlock::Journal.open(dir,context) { } }
  end
  File.binwrite(state_path,original)
  # Real previous snapshot survives a denied rename at the filesystem boundary.
  child=<<~RUBY
    require #{tool.inspect}
    context=JSON.parse(ARGV.fetch(1))
    A133Unlock::Journal.open(ARGV.fetch(0),context) do |store|
      File.singleton_class.prepend(Module.new do
        def rename(*args); raise Errno::EIO, 'PRIVATE_RENAME'; end
      end)
      begin
        store.record('restoring',write:true)
        abort 'FAIL: rename fault swallowed'
      rescue A133Unlock::Invalid => error
        abort 'FAIL: raw fault escaped' unless error.message=='unlock_journal_io_failed' && error.cause.nil?
      end
    end
  RUBY
  out,err,status=Open3.capture3(RbConfig.ruby,'-e',child,dir,JSON.generate(context))
  abort 'FAIL: journal publication fault' unless status.success? && out.empty? && err.empty? && File.binread(state_path)==original
  checks+=1
end
puts "Unlock journal: #{checks} private persistence/exclusion/refusal cases passed"
