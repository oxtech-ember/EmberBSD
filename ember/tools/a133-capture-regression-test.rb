#!/usr/bin/env ruby
# Origin: EmberBSD - independent review regressions for capture lifetime and privacy.
require 'tmpdir'
require_relative 'a133-capture-check'
require_relative 'a133-capture-test-support'
CaptureFixture.configure
module CaptureLateMutation
  def read(*args)
    value = super
    if $capture_late_mutation && File.basename(path)=='recovery.bin'
      target = $capture_late_mutation
      $capture_late_mutation = nil
      File.open(target,'ab') { |file| file.write('x') }
    end
    value
  end
end
File.prepend(CaptureLateMutation)
failures = []
checks = 0
skipped = 0
check = lambda do |name,&block|
  if block.call
    checks += 1
  else
    failures << name
  end
end
reject = lambda do |reason,&block|
  begin
    block.call
    false
  rescue A133Capture::Invalid => error
    error.message==reason
  end
end
Dir.mktmpdir('capture-regression-') do |dir|
  manifest = CaptureFixture.write(dir)
  path = File.join(dir,'capture.json')
  run = -> {A133Capture.verify(path,serial:'CAPTURE_TEST_SERIAL',cid:'0123456789abcdef0123456789abcdef')}
  $capture_late_mutation = File.join(dir,'bootloader.bin')
  check.call('late critical-copy mutation must stop') { reject.call('capture_file_changed') {run.call} }
  $capture_late_mutation = nil
  CaptureFixture.write(dir)
  # Two real names resolve to one nlink1 inode on case-insensitive filesystems.
  if File.exist?(File.join(dir,'BOOT.bin'))
    bytes = File.binread(File.join(dir,'disk.raw'))
    bytes[82*512,8192] = 'c'*8192
    File.binwrite(File.join(dir,'disk.raw'),bytes)
    manifest['uncompressed_sha256'] = Digest::SHA256.hexdigest(bytes)
    manifest['partitions'][3]['file'] = 'BOOT.bin'
    File.unlink(File.join(dir,'recovery.bin'))
    File.write(path,JSON.generate(manifest))
    check.call('case aliases must not count as separate copies') do
      reject.call('duplicate_capture_inode') {run.call}
    end
  else
    skipped += 1
  end
  CaptureFixture.write(dir)
  check.call('capture file errors must discard private causes') do
    begin
      A133Capture.verify(File.join(dir,'PRIVATE_CAPTURE_PATH'),serial:'CAPTURE_TEST_SERIAL',
        cid:'0123456789abcdef0123456789abcdef')
      false
    rescue A133Capture::Invalid => error
      error.cause.nil? && !error.full_message.include?('PRIVATE_CAPTURE_PATH')
    end
  end
  File.write(path,'{"PRIVATE_CAPTURE_CONTENT":')
  check.call('parser errors must discard private causes') do
    begin
      run.call
      false
    rescue A133Capture::Invalid => error
      error.message=='invalid_capture_json' && error.cause.nil? &&
        !error.full_message.include?('PRIVATE_CAPTURE_CONTENT')
    end
  end
  check.call('backup file errors must discard private causes') do
    begin
      A133Backup.verify_file(File.join(dir,'PRIVATE_BACKUP_PATH'),sha256:'0'*64)
      false
    rescue A133Backup::Invalid => error
      error.cause.nil? && !error.full_message.include?('PRIVATE_BACKUP_PATH')
    end
  end
  begin
    raise 'PRIVATE_OUTER_ERROR'
  rescue RuntimeError
    check.call('backup validation must discard inherited outer cause') do
      begin
        A133Backup.verify_file(path,sha256:nil)
        false
      rescue A133Backup::Invalid => error
        error.cause.nil? && !error.full_message.include?('PRIVATE_OUTER_ERROR')
      end
    end
  end
end
abort failures.map { |name| "FAIL: #{name}" }.join("\n") unless failures.empty?
puts "Capture review regressions: #{checks} cases passed, #{skipped} case-insensitive alias cases inapplicable"
