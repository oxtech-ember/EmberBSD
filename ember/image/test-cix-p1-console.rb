#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Execute the trusted console policy with real, individually owned host children.
require 'fileutils'
require 'shellwords'
require 'tmpdir'

shell = ENV.fetch('TEST_SH', '/bin/sh')
here = __dir__
wrapper = File.read(File.join(here, 'cix-p1-console'))
checks = 0
check = lambda do |condition, message|
  checks += 1
  abort "FAIL: #{message}" unless condition
end
clock = -> { Process.clock_gettime(Process::CLOCK_MONOTONIC) }
Dir.mktmpdir('cix-console-') do |tmp|
  env_path = File.join(tmp, 'console.env')
  check.call(File.file?(File.join(here, 'cix-p1-console.env')), 'trusted console ENV exists')
  FileUtils.cp(File.join(here, 'cix-p1-console.env'), env_path)
  check.call(wrapper.include?("ENV=/etc/cix-console.env\n") && wrapper.match?(/^export .*\bENV\b/),
    'wrapper exports the fixed trusted console ENV')
  candidate = wrapper.sub('HOME=/root', 'HOME=' + Shellwords.escape(tmp))
    .sub('ENV=/etc/cix-console.env', 'ENV=' + Shellwords.escape(env_path))
    .sub('cd /root', 'cd ' + Shellwords.escape(tmp))
    .sub('exec /bin/sh -i', 'exec ' + Shellwords.escape(shell) + ' -i')
  candidate_path = File.join(tmp, 'console')
  File.write(candidate_path, candidate)
  run = lambda do |argv, env, expected|
    input_read, input_write = IO.pipe
    output_read, output_write = IO.pipe
    pid = nil
    reaped = false
    begin
      pid = Process.spawn(env, *argv, in: input_read, out: output_write, err: output_write)
      input_read.close
      output_write.close
      # Some host shells read ENV only when interactive. Execute the native
      # non-POSIX startup boundary explicitly to verify the file's guard too.
      input_write.write(". \"$ENV\"\n") if expected == :signal
      input_write.write("printf 'CIX_CONSOLE_READY\\n'\n")
      input_write.flush
      deadline = clock.call + 3
      output = +''
      until output.include?("CIX_CONSOLE_READY\n")
        raise 'console readiness timeout' if clock.call >= deadline
        next unless IO.select([output_read], nil, nil, 0.05)
        output << output_read.read_nonblock(4096)
      end
      Process.kill('TERM', pid)
      deadline = clock.call + (expected == :alive ? 0.25 : 3)
      result = nil
      loop do
        result = Process.waitpid2(pid, Process::WNOHANG)
        if result
          reaped = true
          break
        end
        break if clock.call >= deadline
        sleep 0.01
      end
      if expected == :alive
        check.call(result.nil?, 'interactive baseline ignores TERM before the profile trap')
      elsif expected == :exit
        check.call(result && result[1].exited? && result[1].exitstatus.zero?,
          'interactive profile console exits gracefully on TERM')
      else
        check.call(result && result[1].signaled? && result[1].termsig == Signal.list.fetch('TERM'),
          'noninteractive ENV leaves the native TERM disposition')
      end
    ensure
      # Signal only this unreaped child; never use a process group or broadcast.
      if pid && !reaped
        begin
          Process.kill('KILL', pid)
        rescue Errno::ESRCH
        end
        Process.waitpid(pid)
      end
      [input_read, input_write, output_read, output_write].each { |io| io.close unless io.closed? }
    end
  end
  run.call([shell, '-i'], {'ENV' => ''}, :alive)
  run.call([shell, candidate_path], {'ENV' => ''}, :exit)
  run.call([shell, '-s'], {'ENV' => env_path}, :signal)
end
puts "CIX console TERM: #{checks} checks, 0 failures"
