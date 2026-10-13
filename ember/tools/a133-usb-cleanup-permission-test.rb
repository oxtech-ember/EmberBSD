#!/usr/bin/env ruby
# Origin: EmberBSD - bounded transient and persistent process-group cleanup permission regressions.
require 'tmpdir'
require 'rbconfig'
require 'open3'
tool=File.join(__dir__,'a133-usb-channel.rb')
child=<<~RUBY
  require 'tmpdir'
  require #{tool.inspect}
  Dir.mktmpdir('a133-cleanup-permission-') do |dir|
    adb=File.join(dir,'adb')
    File.write(adb,"#!#{RbConfig.ruby}\nSTDOUT.write('OK')\n")
    File.chmod(0700,adb)
    mode=ARGV.fetch(0); calls=0
    Process.singleton_class.prepend(Module.new do
      define_method(:kill) do |signal,pid|
        if signal=='KILL' && pid<0
          calls+=1
          raise Errno::EPERM,'PRIVATE_GROUP_PERMISSION' if mode=='persistent' || calls==1
        end
        super(signal,pid)
      end
    end)
    started=Process.clock_gettime(Process::CLOCK_MONOTONIC)
    begin
      result=A133Usb::Channel.new(adb,1).run([])
      abort 'FAIL: persistent permission denial accepted' if mode=='persistent'
      abort 'FAIL: transient cleanup lost actual output' unless result[:output]=='OK'
    rescue A133Usb::Invalid => error
      abort 'FAIL: transient permission cleanup not retried' unless mode=='persistent' && error.message=='usb_cleanup_failed'
    end
    abort 'FAIL: permission cleanup unbounded' unless Process.clock_gettime(Process::CLOCK_MONOTONIC)-started<1 && calls<=3
  end
RUBY
failures=[]
%w[transient persistent].each do |mode|
  out,err,status=Open3.capture3(RbConfig.ruby,'-e',child,mode)
  failures << mode+': '+out+err unless status.success? && out.empty? && err.empty?
end
abort failures.join("\n") unless failures.empty?
puts 'USB permission cleanup: 2 real-subprocess transient/persistent cases passed'
