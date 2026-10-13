#!/usr/bin/env ruby
# Origin: EmberBSD - independent small GPT fixtures for backup integrity.
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'stringio'
require 'tmpdir'
require 'zlib'
require 'timeout'
tool = File.join(__dir__, 'a133-backup-check.rb')
abort 'FAIL: full backup verifier is missing' unless File.file?(tool)
require tool

require_relative 'a133-backup-test-support'

checks = 0
data = BackupFixture.disk
sha = Digest::SHA256.hexdigest(data)
run = lambda do |bytes, expected_sha = Digest::SHA256.hexdigest(bytes)|
  A133Backup.verify_stream(StringIO.new(bytes), 131072, expected_sha, BackupFixture::INVENTORY)
end
result = run.call(data)
abort 'FAIL: full or critical-range hash lost' unless result[:uncompressed_sha256] == sha &&
  result[:partition_sha256]['boot'] == Digest::SHA256.hexdigest('Q' * 51200)
checks += 1
reject = lambda do |reason, bytes, expected_sha = Digest::SHA256.hexdigest(bytes)|
  begin
    run.call(bytes, expected_sha)
    abort "FAIL: accepted #{reason}"
  rescue A133Backup::Invalid => e
    abort "FAIL: wanted #{reason}, got #{e.message}" unless e.message == reason
  end
  checks += 1
end
reject.call('disk_size_mismatch', data.byteslice(0, data.size - 1))
reject.call('disk_size_mismatch', data + 'x')
reject.call('disk_sha256_mismatch', data, '0' * 64)
x = data.dup; x.setbyte(100000, 1)
reject.call('disk_sha256_mismatch', x, sha)
x = data.dup; x.setbyte(510, 0)
reject.call('protective_mbr_invalid', x)
x = data.dup; x.setbyte(466, 1)
reject.call('protective_mbr_invalid', x)
x = data.dup; x.setbyte(450, 0x83)
reject.call('protective_mbr_invalid', x)
x = data.dup; x[454, 4] = [2].pack('V')
reject.call('protective_mbr_invalid', x)
x = data.dup; x.setbyte(512, 0)
reject.call('gpt_signature_missing', x)
x = data.dup; x.setbyte(512 + 16, x.getbyte(512 + 16) ^ 1)
reject.call('gpt_header_crc_mismatch', x)
x = data.dup; x[512 + 24, 8] = [2].pack('Q<'); BackupFixture.crc_header(x, 1)
reject.call('gpt_header_layout_mismatch', x)
x = data.dup; x[512 + 32, 8] = [254].pack('Q<'); BackupFixture.crc_header(x, 1)
reject.call('gpt_header_layout_mismatch', x)
x = data.dup; x[512 + 80, 4] = [129].pack('V'); BackupFixture.crc_header(x, 1)
reject.call('gpt_header_layout_mismatch', x)
x = data.dup; x[255 * 512 + 56, 16] = 'E' * 16; BackupFixture.crc_header(x, 255)
reject.call('gpt_copies_differ', x)
x = data.dup; x[255 * 512 + 72, 8] = [200].pack('Q<'); BackupFixture.crc_header(x, 255)
reject.call('gpt_header_layout_mismatch', x)
x = data.dup; x.setbyte(1024, 0)
reject.call('gpt_array_crc_mismatch', x)
x = data.dup; x.setbyte(223 * 512 + 56, 120); BackupFixture.arrays_crc(x)
reject.call('gpt_copies_differ', x)
x = data.dup
[1024, 223 * 512].each { |o| x[o, 16] = "\0" * 16 }
BackupFixture.arrays_crc(x)
reject.call('partition_layout_mismatch', x)
x = data.dup
[1024, 223 * 512].each { |o| x[o + 32, 8] = [35].pack('Q<') }
BackupFixture.arrays_crc(x)
reject.call('partition_layout_mismatch', x)
x = data.dup
[1024, 223 * 512].each { |o| x[o + 56, 2] = 'x'.encode('UTF-16LE').b }
BackupFixture.arrays_crc(x)
reject.call('partition_layout_mismatch', x)

Dir.mktmpdir('a133-backup-') do |dir|
  input = File.join(dir, 'image')
  decoder = File.join(dir, 'decoder')
  File.binwrite(input, data)
  File.write(decoder, <<~'DECODER')
    #!/usr/bin/env ruby
    abort 'HOST_SECRET_DO_NOT_PRINT' if ENV['DECODER_CASE'] == 'failure'
    if %w[noise noise_ok].include?(ENV['DECODER_CASE'])
      STDERR.write('HOST_SECRET_DO_NOT_PRINT' * 100000)
      exit(ENV['DECODER_CASE'] == 'noise_ok' ? 0 : 2)
    end
    if ENV['DECODER_CASE'] == 'stall'
      fork { sleep 20 }
      sleep 20
    end
    if ENV['DECODER_CASE'] == 'orphan_stderr'
      child = fork { STDOUT.close; sleep 20 }
      File.write(ENV.fetch('DECODER_PID_FILE'), child.to_s)
      exit 0
    end
    STDOUT.write(STDIN.read)
  DECODER
  File.chmod(0700, decoder)
  %w[failure noise noise_ok stall orphan_stderr].each do |mode|
    start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    out, err, status = Timeout.timeout(5) do
      Open3.capture3({'DECODER_CASE' => mode, 'DECODER_PID_FILE' => File.join(dir, 'child.pid')}, RbConfig.ruby, tool,
        '--sha256', sha, '--zstd', decoder, '--timeout', '1', input)
    end
    result = JSON.parse(out)
    reason = %w[stall orphan_stderr].include?(mode) ? 'backup_read_timeout' : 'decompression_failed'
    abort "FAIL: decoder #{mode}: #{out} #{err}" unless !status.success? && err.empty? &&
      result['reason'] == reason && result['installation_ready'] == false &&
      result['writes_performed'] == 0 && !out.include?(dir) && !out.include?('HOST_SECRET') &&
      Process.clock_gettime(Process::CLOCK_MONOTONIC) - start < 5
    if mode == 'orphan_stderr'
      child = Integer(File.read(File.join(dir, 'child.pid')))
      reap_deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 0.5
      begin
        loop do
          Process.kill(0, child)
          abort 'FAIL: decoder descendant survived deadline' if
            Process.clock_gettime(Process::CLOCK_MONOTONIC) >= reap_deadline
          sleep 0.02
        end
      rescue Errno::ESRCH
        # Allow a bounded interval for the system reaper to remove a killed orphan.
      end
    end
    checks += 1
  end
  File.symlink(input, File.join(dir, 'link'))
  [ [File.join(dir, 'link'), 'backup_file_not_regular'],
    [input, 'disk_size_mismatch'] ].each do |path, reason|
    out, err, status = Open3.capture3(RbConfig.ruby, tool, '--sha256', sha, '--format', 'raw', path)
    abort 'FAIL: raw/symlink guard' unless !status.success? && err.empty? && JSON.parse(out)['reason'] == reason
    checks += 1
  end
  File.binwrite(input, '')
  out, err, status = Open3.capture3(RbConfig.ruby, tool, '--sha256', sha, '--format', 'raw', input)
  abort 'FAIL: empty backup guard' unless !status.success? && err.empty? && JSON.parse(out)['reason'] == 'disk_size_mismatch'
  checks += 1
end
puts "Backup integrity: #{checks} small-image and decoder cases passed"
