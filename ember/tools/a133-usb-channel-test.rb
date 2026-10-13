#!/usr/bin/env ruby
# Origin: EmberBSD - actual subprocess contracts for bounded binary USB streams.
require 'digest'
require 'rbconfig'
require 'stringio'
require 'tmpdir'
require_relative 'a133-usb-channel'

checks = 0
Dir.mktmpdir('a133-channel-') do |dir|
  child = File.join(dir, 'child')
  File.write(child, <<~'CHILD')
    #!/usr/bin/env ruby
    STDIN.binmode; STDOUT.binmode; STDERR.binmode
    case ARGV.first
    when 'echo'
      while (chunk = STDIN.read(8192)); STDOUT.write(chunk); end
    when 'failure'
      STDERR.write('HOST_SECRET'); exit 2
    when 'noise'
      out = Thread.new { STDOUT.write('x' * 2000000) }
      STDERR.write('HOST_SECRET' * 200000)
      out.join
    when 'large'
      STDOUT.write('x' * 2000000)
    when 'orphan'
      pid = fork { STDOUT.close; sleep 20 }
      File.write(ARGV.fetch(1), pid.to_s)
      exit 0
    when 'stall'
      sleep 20
    when 'close'
      STDIN.close
      exit 3
    else
      abort 'unsupported fixture'
    end
  CHILD
  File.chmod(0700, child)
  channel = A133Usb::Channel.new(child, 1)
  binary = ("\0\r\n\xffABC".b * 200000)
  result = channel.run(['echo'], source: StringIO.new(binary), input_bytes: binary.bytesize,
    limit: binary.bytesize)
  abort 'FAIL: binary stream changed' unless result[:output] == binary &&
    result[:input_sha256] == Digest::SHA256.hexdigest(binary)
  checks += 1
  sha = Digest::SHA256.new
  result = channel.run(['echo'], source: StringIO.new(binary), input_bytes: 512) { |chunk| sha.update(chunk) }
  abort 'FAIL: input bound ignored' unless sha.hexdigest == Digest::SHA256.hexdigest(binary.byteslice(0, 512)) &&
    result[:input_sha256] == sha.hexdigest && result[:output].empty?
  checks += 1
  [ ['failure', 'usb_command_failed'], ['noise', 'usb_diagnostics'],
    ['large', 'usb_output_too_large'], ['stall', 'usb_timeout'],
    ['orphan', 'usb_timeout'] ].each do |mode, reason|
    pid_path = File.join(dir, 'descendant.pid')
    start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    begin
      if mode == 'noise'
        channel.run([mode]) { |_chunk| }
      else
        channel.run([mode, pid_path])
      end
      abort "FAIL: accepted #{mode}"
    rescue A133Usb::Invalid => error
      abort "FAIL: #{mode}: #{error.message}" unless error.message == reason &&
        Process.clock_gettime(Process::CLOCK_MONOTONIC) - start < 4
    end
    if mode == 'orphan'
      pid = Integer(File.read(pid_path))
      deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 0.5
      begin
        loop do
          Process.kill(0, pid)
          abort 'FAIL: surviving USB descendant' if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
          sleep 0.02
        end
      rescue Errno::ESRCH
        # Allow bounded system reaping after process-group termination.
      end
    end
    checks += 1
  end
  begin
    channel.run(['echo'], source: StringIO.new('x'), input_bytes: 512)
    abort 'FAIL: short input accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: short input reason' unless error.message == 'usb_input_short'
  end
  checks += 1
  begin
    channel.run(['close'], source: StringIO.new(binary), input_bytes: binary.bytesize)
    abort 'FAIL: closed receiver accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: closed receiver reason' unless %w[usb_command_failed usb_pipe_failed].include?(error.message)
  end
  checks += 1
  # Fault injection at the observed OS signal boundary; subprocess I/O remains real.
  original_kill = Process.method(:kill)
  Process.define_singleton_method(:kill) do |*args|
    begin
      original_kill.call(*args)
    rescue Errno::ESRCH
      # The real fixture child is already gone.
    end
    raise Errno::EPERM
  end
  begin
    channel.run(['failure'])
    abort 'FAIL: command failure accepted during cleanup error'
  rescue A133Usb::Invalid => error
    abort 'FAIL: cleanup hid primary failure' unless error.message == 'usb_command_failed'
  ensure
    Process.define_singleton_method(:kill,original_kill)
  end
  checks += 1
  Process.define_singleton_method(:kill) do |*args|
    begin
      original_kill.call(*args)
    rescue Errno::ESRCH
    end
    raise Errno::EPERM
  end
  begin
    channel.run(['echo'])
    abort 'FAIL: successful cleanup claim after signal failure'
  rescue A133Usb::Invalid => error
    abort 'FAIL: cleanup failure reason' unless error.message == 'usb_cleanup_failed'
    begin
      raise RuntimeError, 'outer caller failure'
    rescue RuntimeError
      begin
        channel.run(['echo'])
        abort 'FAIL: inherited rescue concealed cleanup failure'
      rescue A133Usb::Invalid => nested
        abort 'FAIL: nested cleanup reason' unless nested.message == 'usb_cleanup_failed'
      end
    end
  ensure
    Process.define_singleton_method(:kill,original_kill)
  end
  checks += 1
  # Simulate a real signal refusal while the actual child remains alive.
  checks += 1
  blocked_pid = nil
  Process.define_singleton_method(:kill) do |_,group|
    blocked_pid = -group
    raise Errno::EPERM
  end
  start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  begin
    Timeout.timeout(4) { channel.run(['stall']) }
    abort 'FAIL: live-child cleanup refusal accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: live-child primary deadline lost' unless error.message == 'usb_timeout' &&
      Process.clock_gettime(Process::CLOCK_MONOTONIC) - start < 4
  ensure
    Process.define_singleton_method(:kill,original_kill)
    begin
      original_kill.call('KILL',-blocked_pid) if blocked_pid
    rescue Errno::ESRCH
      # Test owns cleanup of the intentionally unkillable fixture.
    end
  end
  checks += 1
end
puts "USB channel: #{checks} actual subprocess cases passed"
