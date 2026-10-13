#!/usr/bin/env ruby
# Origin: EmberBSD - real-file callable backup and decoder lifecycle contracts.
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require_relative 'a133-backup-check'
require_relative 'a133-backup-test-support'

abort 'FAIL: callable file verifier is missing' unless A133Backup.respond_to?(:verify_file)
out, err, status = Open3.capture3(RbConfig.ruby, '-e', "require ARGV.fetch(0)",
  File.join(__dir__, 'a133-backup-check.rb'))
abort 'FAIL: import performed CLI work' unless status.success? && out.empty? && err.empty?
# Only the test process uses small geometry; production has no fixture mode.
A133Backup.send(:remove_const, :BYTES)
A133Backup.const_set(:BYTES, 131072)
A133Backup.send(:remove_const, :INVENTORY)
A133Backup.const_set(:INVENTORY, BackupFixture::INVENTORY)
checks = 1
Dir.mktmpdir('backup-api-') do |dir|
  input = File.join(dir, 'image')
  decoder = File.join(dir, 'decoder')
  data = BackupFixture.disk
  sha = Digest::SHA256.hexdigest(data)
  File.binwrite(input, data)
  File.write(decoder, <<~'SCRIPT')
    #!/usr/bin/env ruby
    data = STDIN.read
    case ENV['BACKUP_API_CASE']
    when 'mutate'
      File.open(ENV.fetch('BACKUP_API_SOURCE'), 'ab') { |f| f.write('x') }
    when 'failure'
      STDERR.write('PRIVATE_DECODER_DIAGNOSTIC')
      exit 2
    when 'stall'
      sleep 10
    end
    STDOUT.write(data)
  SCRIPT
  File.chmod(0700, decoder)
  verify = ->(**options) { A133Backup.verify_file(input, sha256: sha, **options) }
  %w[raw zstd].each do |format|
    receipt = verify.call(format: format, zstd: decoder)
    abort 'FAIL: real file receipt lost' unless receipt[:status] == 'backup_integrity_verified' &&
      receipt[:bytes] == 131072 && receipt[:uncompressed_sha256] == sha &&
      receipt[:partition_sha256]['boot'] == Digest::SHA256.hexdigest('Q' * 51200) &&
      receipt[:writes_performed] == 0 && receipt[:installation_ready] == false &&
      receipt[:filesystem_consistency] == 'not_established_by_integrity_check'
    checks += 1
  end
  reject = lambda do |reason, &block|
    begin
      block.call
      abort "FAIL: accepted #{reason}"
    rescue A133Backup::Invalid => error
      abort "FAIL: wanted #{reason}, got #{error.message}" unless error.message == reason
    end
    checks += 1
  end
  reject.call('disk_sha256_mismatch') { A133Backup.verify_file(input, sha256: '0' * 64, format: 'raw') }
  reject.call('invalid_arguments') { verify.call(format: 'zip') }
  reject.call('invalid_arguments') { verify.call(timeout: 0) }
  reject.call('invalid_arguments') { A133Backup.verify_file(input, sha256: nil) }
  File.symlink(input, File.join(dir, 'link'))
  reject.call('backup_file_not_regular') { A133Backup.verify_file(File.join(dir, 'link'), sha256: sha) }
  reject.call('backup_read_failed') { A133Backup.verify_file(File.join(dir, 'absent'), sha256: sha) }
  %w[mutate failure stall].each do |mode|
    ENV['BACKUP_API_CASE'] = mode
    ENV['BACKUP_API_SOURCE'] = input
    reason = {'mutate'=>'backup_source_changed', 'failure'=>'decompression_failed', 'stall'=>'backup_read_timeout'}.fetch(mode)
    reject.call(reason) { verify.call(zstd: decoder, timeout: 1) }
    File.binwrite(input, data)
  end
  ENV.delete('BACKUP_API_CASE')
  ENV.delete('BACKUP_API_SOURCE')
  File.binwrite(input, data.byteslice(0, data.size - 1))
  reject.call('disk_size_mismatch') { verify.call(format: 'raw') }
  File.binwrite(input, data)
  begin
    raise 'outer error'
  rescue RuntimeError
    receipt = verify.call(zstd: decoder)
    abort 'FAIL: outer rescue broke decoder cleanup' unless receipt[:bytes] == 131072
    checks += 1
  end
end
puts "Backup library: #{checks} real-file and decoder cases passed"
