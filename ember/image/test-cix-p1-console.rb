#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the actual trusted console wrapper in an owned controlling PTY.
require 'pty'
require 'shellwords'
require 'tmpdir'

shell = ENV.fetch('TEST_SH', '/bin/sh')
wrapper = File.read(File.join(__dir__, 'cix-p1-console'))
checks = 0
check = lambda do |condition, message|
  checks += 1
  abort "FAIL: #{message}" unless condition
end
clock = -> { Process.clock_gettime(Process::CLOCK_MONOTONIC) }
Dir.mktmpdir('cix-console-') do |tmp|
  candidate = wrapper.sub('HOME=/root', 'HOME=' + Shellwords.escape(tmp))
    .sub('cd /root', 'cd ' + Shellwords.escape(tmp))
    .gsub('/bin/sh -i', Shellwords.escape(shell) + ' -i')
  candidate_path = File.join(tmp, 'console')
  File.write(candidate_path, candidate)
  run = lambda do |path, scenario|
    reader = writer = pid = child = command = nil
    reaped = false
    output = +''
    eof = false
    status = nil
    begin
      reader, writer, pid = PTY.spawn({'ENV' => '', 'TERM' => 'dumb'}, shell, path)
      drain = lambda do |seconds|
        if !eof && IO.select([reader], nil, nil, seconds)
          begin
            bytes = reader.read_nonblock(4096, exception: false)
            if bytes.nil?
              eof = true
            elsif bytes.is_a?(String)
              output << bytes
            end
          rescue Errno::EIO, EOFError
            eof = true
          end
        end
      end
      marker = lambda do |name|
        deadline = clock.call + 3
        until (match = output.match(/#{name}:(\d+)\r?\n/))
          raise "#{name} timeout: #{output.inspect}" if eof || clock.call >= deadline
          drain.call(0.02)
        end
        match[1].to_i
      end
      writer.write("printf 'CIX_SHELL:%s\\n' \"$$\"\n")
      writer.flush
      child = marker.call('CIX_SHELL')
      check.call(Process.getsid(pid) == pid && Process.getsid(child) == pid,
        'wrapper and interactive shell share the owned controlling session')
      if scenario == :baseline
        check.call(child == pid, 'old exec wrapper replaces its session leader with the interactive shell')
      else
        check.call(child != pid && Process.getpgid(child) == child,
          'noninteractive session leader retains a child with normal interactive job control')
      end
      if scenario == :normal
        # A normal terminal interrupt must leave both the prompt and leader alive.
        deadline = clock.call + 3
        until output.end_with?('ember-cix# ')
          raise "initial prompt timeout: #{output.inspect}" if eof || clock.call >= deadline
          drain.call(0.02)
        end
        interrupt = lambda do
          offset = output.bytesize
          writer.write("\003")
          writer.flush
          deadline = clock.call + 3
          until output.byteslice(offset..).include?('ember-cix# ')
            raise "interrupt prompt timeout: #{output.inspect}" if eof || clock.call >= deadline
            drain.call(0.02)
          end
        end
        interrupt.call
        writer.write("printf 'CIX_IDLE:%s\\n' \"$$\"\n")
        writer.flush
        check.call(marker.call('CIX_IDLE') == child, 'Ctrl-C at the idle prompt keeps the interactive shell')
        body = "printf 'CIX_COMMAND:%s\\n' \"$$\"; exec sleep 30"
        writer.write(Shellwords.escape(shell) + ' -c ' + Shellwords.escape(body) + "\n")
        writer.flush
        command = marker.call('CIX_COMMAND')
        check.call(Process.getsid(command) == pid && Process.getpgid(command) != Process.getpgid(child),
          'foreground command has its own job process group in the console session')
        interrupt.call
        writer.write("printf 'CIX_AFTER_INT:%s\\n' \"$$\"\n")
        writer.flush
        check.call(marker.call('CIX_AFTER_INT') == child,
          'Ctrl-C stops the foreground command and returns to the same prompt')
        command = nil # The interactive shell has reaped its foreground command.
        writer.write("exit 7\n")
        writer.flush
      else
        # No terminal input follows TERM. Only this owned session leader is signaled.
        Process.kill('TERM', pid)
      end
      deadline = clock.call + (scenario == :baseline ? 0.25 : 3)
      loop do
        unless reaped
          result = Process.waitpid2(pid, Process::WNOHANG)
          status = result.last if result
          reaped = !result.nil?
        end
        drain.call(0.01)
        break if (reaped && eof) || clock.call >= deadline
      end
      diagnostic = "status=#{status.inspect}, PTY_EOF=#{eof}, output=#{output.inspect}"
      if scenario == :baseline
        check.call(!reaped && !eof, "idle interactive exec baseline survives TERM: #{diagnostic}")
      elsif scenario == :normal
        check.call(reaped && status.exited? && status.exitstatus == 7 && eof,
          "wrapper preserves normal child exit status: #{diagnostic}")
      else
        check.call(reaped && status.signaled? && status.termsig == Signal.list.fetch('TERM') && eof,
          "leader TERM closes the idle console without further input: #{diagnostic}")
      end
      child = nil if eof # All slave descriptors closed; do not signal a released PID.
    ensure
      # Never broadcast or signal a process group. Clean up only tracked children.
      [command, child].compact.uniq.reject { |owned| owned == pid }.each do |owned|
        begin
          Process.kill('KILL', owned)
        rescue Errno::ESRCH
        end
      end
      if pid && !reaped
        begin
          Process.kill('KILL', pid)
        rescue Errno::ESRCH
        end
      end
      # Close the PTY before waiting: a dying session leader can drain output
      # in the kernel. Do not leave unread output blocking failure cleanup.
      [reader, writer].compact.each { |io| io.close unless io.closed? }
      Process.waitpid(pid) if pid && !reaped
    end
  end
  run.call(candidate_path, :term)
  run.call(candidate_path, :normal)
  # Execute the previous lifecycle as a causal control, with the same setup.
  baseline = candidate.sub(/^(#{Regexp.escape(Shellwords.escape(shell))} -i)\n.*\z/m) { "exec #{Regexp.last_match(1)}\n" }
  baseline_path = File.join(tmp, 'console-exec')
  File.write(baseline_path, baseline)
  run.call(baseline_path, :baseline)
end
puts "CIX console PTY lifecycle: #{checks} checks, 0 failures"
