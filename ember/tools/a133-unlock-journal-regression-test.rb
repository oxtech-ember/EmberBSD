#!/usr/bin/env ruby
# Origin: EmberBSD - real-file post-rename intent and lock-scope regressions.
require 'open3'
require 'rbconfig'
tool=File.join(__dir__,'a133-unlock-journal.rb')
# Faults preserve the actual filesystem side effect; each case has its own process.
child=<<~RUBY
  require 'tmpdir'
  require #{tool.inspect}
  context={'serial'=>'JOURNAL_TEST','cid'=>'0'*32}
  %w[backup gpt bootloader boot recovery original_env unlock_env mutable].each { |key| context[key+'_sha256']='a'*64 }
  Dir.mktmpdir('a133-journal-fault-') do |base|
    dir=File.join(base,'session')
    A133Unlock::Journal.open(dir,context) do |first|
      if ARGV.first=='fsync'
        first.record('armed',write:true,hardware_bytes:4194304)
        module UnlockJournalFsyncFault
          class << self; attr_accessor :active; end
          def fsync
            value=super
            if UnlockJournalFsyncFault.active && stat.directory?
              UnlockJournalFsyncFault.active=false
              raise Errno::EIO,'PRIVATE_DIRECTORY_FSYNC'
            end
            value
          end
        end
        File.prepend(UnlockJournalFsyncFault)
        UnlockJournalFsyncFault.active=true
        begin
          first.record('rebooting',reboot:true)
          abort 'FAIL: directory fsync failure ignored'
        rescue A133Unlock::Invalid => error
          abort 'FAIL: raw fsync diagnostic' unless error.message=='unlock_journal_io_failed' && error.cause.nil?
        end
        abort 'FAIL: published reboot marker missing' unless JSON.parse(File.read(File.join(dir,'state.json')))['possible_reboot']
        first.record('failed',reason:'unlock_journal_io_failed')
        abort 'FAIL: failed record erased published reboot intent' unless first.report[:possible_reboot] &&
          JSON.parse(File.read(File.join(dir,'state.json')))['possible_reboot']
      else
        if ARGV.first=='directory'
          File.rename(dir,dir+'.moved')
        elsif ARGV.first=='lock'
          File.rename(File.join(dir,'.lock'),File.join(dir,'.lock.moved'))
        elsif ARGV.first=='permissions'
          File.chmod(0755,dir)
        else
          abort 'FAIL: unknown regression case'
        end
        attempt=lambda do
          begin
            first.record('rebooting',reboot:true)
            abort 'FAIL: detached lock scope accepted a write'
          rescue A133Unlock::Invalid => error
            abort 'FAIL: private scope diagnostic' unless error.cause.nil? &&
              error.message.match?(/\\A[a-z0-9_]{1,64}\\z/)
          end
        end
        if ARGV.first=='permissions'
          before=File.binread(File.join(dir,'state.json'))
          attempt.call
          abort 'FAIL: scope refusal changed bytes' unless File.binread(File.join(dir,'state.json'))==before
          File.chmod(0700,dir)
        else
          A133Unlock::Journal.open(dir,context) do |second|
            before=File.binread(File.join(dir,'state.json'))
            attempt.call
            abort 'FAIL: old Store overwrote replacement journal' unless
              File.binread(File.join(dir,'state.json'))==before && second.report[:possible_reboot]==false
          end
        end
      end
    end
    if ARGV.first=='fsync'
      A133Unlock::Journal.open(dir,nil) { |store| abort 'FAIL: restart lost intent' unless store.report[:possible_reboot] }
    end
  end
RUBY
cases=ARGV.empty? ? %w[fsync directory lock permissions] : ARGV
failures=[]
cases.each do |name|
  out,err,status=Open3.capture3(RbConfig.ruby,'-e',child,name)
  failures << "#{name}: #{out}#{err}" unless status.success? && out.empty? && err.empty?
end
abort failures.join("\n") unless failures.empty?
puts "Unlock journal regressions: #{cases.size} post-rename and scope cases passed"
