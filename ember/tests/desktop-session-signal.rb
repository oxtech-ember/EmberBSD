# Origin: EmberBSD real D-Bus launcher termination regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'timeout'
require 'shellwords'
dbus=File.expand_path(ARGV.fetch(0){abort 'Usage: ruby desktop-session-signal.rb native-dbus-run-session'})
abort 'Missing native D-Bus session runner' unless File.executable?(dbus)
daemon=File.join(File.dirname(dbus),'dbus-daemon')
abort 'Missing adjacent native D-Bus daemon' unless File.executable?(daemon)
root=File.expand_path('../..',__dir__)
source=File.read(root+'/libexec/ember-desktop/ember-desktop')
Dir.mktmpdir('ember-bus-', '/tmp') do |d|
  FileUtils.mkdir_p(d+'/pkg/bin')
  FileUtils.mkdir_p(d+'/etc')
  File.write(d+'/etc/ember-desktop',"plasma\n")
  File.write(d+'/session.conf',"<busconfig><type>session</type><listen>unix:tmpdir=#{d}</listen><auth>EXTERNAL</auth><policy context=\"default\"><allow own=\"*\"/><allow send_destination=\"*\"/><allow receive_sender=\"*\"/></policy></busconfig>\n")
  File.write(d+'/pkg/bin/dbus-run-session',"#!/bin/sh\nexec #{Shellwords.escape(dbus)} --dbus-daemon=#{Shellwords.escape(daemon)} --config-file=#{Shellwords.escape(d+'/session.conf')} \"$@\"\n")
  File.write(d+'/pkg/bin/startplasma-wayland',"#!/bin/sh\nprintf '%s\\n' \"$$\" \"$XDG_RUNTIME_DIR\" > \"$TEST_READY\"\nexec /bin/sleep 30\n")
  %w[dbus-run-session startplasma-wayland].each{|name|File.chmod(0755,d+'/pkg/bin/'+name)}
  script=source.gsub('/etc/ember-desktop',d+'/etc/ember-desktop').gsub('/usr/pkg',d+'/pkg')
  env={'XDG_RUNTIME_DIR'=>nil,'TEST_READY'=>d+'/ready'}
  Open3.popen3(env,'sh','-eu','-c',script) do |input,output,error,waiter|
    input.close
    app=nil
    begin
      Timeout.timeout(5) do
        until File.file?(d+'/ready') && File.read(d+'/ready').lines.size==2
          abort 'D-Bus runner failed: '+error.read if waiter.join(0)
          sleep 0.02
        end
      end
      pid,runtime=File.read(d+'/ready').lines.map(&:strip)
      app=Integer(pid)
      Process.kill('TERM',waiter.pid)
      status=Timeout.timeout(3){waiter.value}
      abort 'TERM did not preserve the launcher signal status' unless status.exitstatus==143
      abort 'TERM left its runtime directory behind' if File.exist?(runtime)
      Timeout.timeout(3) do
        loop do
          stat,=Open3.capture2('ps','-o','stat=','-p',app.to_s)
          break if stat.strip.empty? || stat.lstrip.start_with?('Z')
          sleep 0.02
        end
      end
    ensure
      unless waiter.join(0)
        Process.kill('KILL',waiter.pid) rescue Errno::ESRCH
      end
      Process.kill('KILL',app) rescue Errno::ESRCH if app
    end
  end
end
puts 'PASS: TERM reaches the real D-Bus child session; launcher exits and cleans its private runtime'
