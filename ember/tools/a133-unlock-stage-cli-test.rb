#!/usr/bin/env ruby
# Origin: EmberBSD - inspection-only unlock journal CLI including late host failure.
require 'tmpdir'
require 'rbconfig'
require 'open3'
require_relative 'a133-unlock-journal'
tool=File.join(__dir__,'a133-unlock-stage.rb')
context={'serial'=>'CLI_ONLY','cid'=>'0'*32}
%w[backup gpt bootloader boot recovery original_env unlock_env mutable].each { |key| context[key+'_sha256']='a'*64 }
Dir.mktmpdir('a133-unlock-inspect-') do |dir|
  session=File.join(dir,'session')
  A133Unlock::Journal.open(session,context) { }
  out,err,status=Open3.capture3(RbConfig.ruby,tool,session)
  result=JSON.parse(out)
  abort 'FAIL: journal CLI inspection' unless status.success? && err.empty? && result['phase']=='checking' &&
    result['installation_ready']==false && !out.include?(dir) && !out.include?('CLI_ONLY')
  preload=File.join(dir,'late-fault.rb')
  File.write(preload,<<~RUBY)
    File.singleton_class.prepend(Module.new do
      def open(*args,**options,&block)
        value=super
        raise Errno::EIO, 'PRIVATE_CLOSE' if args.first.to_s.end_with?('/.lock')
        value
      end
    end)
  RUBY
  out,err,status=Open3.capture3(RbConfig.ruby,'-r',preload,tool,session)
  begin
    result=JSON.parse(out)
  rescue JSON::ParserError
    abort 'FAIL: inspection printed success before late journal failure'
  end
  abort 'FAIL: late inspection fault leaked or reported success' unless !status.success? && err.empty? &&
    result['status']=='unlock_inspection_stopped' && result['reason']=='unlock_journal_io_failed' && !out.include?(dir)
end
puts 'Unlock inspection: 2 read-only and late-failure cases passed'
