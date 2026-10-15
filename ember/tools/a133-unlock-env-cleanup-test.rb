#!/usr/bin/env ruby
# Origin: EmberBSD - real permission-denied cleanup before and after env publication.
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'zlib'

abort 'FAIL: cleanup permission contract requires a non-root test process' if Process.uid == 0
tool = File.join(__dir__, 'a133-unlock-env.rb')
vars = {
  'bootcmd' => 'run setargs_mmc boot_normal',
  'boot_normal' => 'run ${hook};run boot_android',
  'boot_android' => 'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery' => 'sunxi_flash read 45000000 recovery;bootm 45000000'
}
body = vars.map { |key, value| key + '=' + value + "\0" }.join + "\0"
body = body.ljust(131068, "\0")
original = [Zlib.crc32(body)].pack('V') + body
checks = 0
phases = ARGV.empty? ? %w[before after inaccessible] : ARGV
abort 'FAIL: invalid cleanup test phase' unless phases.all? { |phase| %w[before after inaccessible].include?(phase) }
phases.each do |phase|
  Dir.mktmpdir('a133-unlock-cleanup-') do |dir|
    input, output, preload = %w[original candidate fault.rb].map { |name| File.join(dir, name) }
    File.binwrite(input, original)
    # All writes/publication stay real. Restrict only this fixture directory
    # at the link boundary so native link/unlink/lstat produce EACCES.
    File.write(preload, <<~RUBY)
      File.singleton_class.prepend(Module.new do
        def link(source, destination)
          phase = ENV.fetch('A133_TEST_CLEANUP_PHASE')
          directory = File.dirname(destination)
          if phase == 'before'
            File.chmod(0555, directory)
            super
          else
            result = super
            File.chmod(phase == 'inaccessible' ? 0000 : 0555, directory)
            result
          end
        end
      end)
    RUBY
    begin
      out, err, status = Open3.capture3({'A133_TEST_CLEANUP_PHASE' => phase},
        RbConfig.ruby, '-r', preload, tool, input, output)
    ensure
      File.chmod(0700, dir)
    end
    result = JSON.parse(out)
    expected_reason = phase == 'before' ? 'host_file_error' : 'host_cleanup_failed'
    published = phase != 'before'
    abort "FAIL: #{phase} cleanup escaped the redacted failure boundary" unless
      !status.success? && err.empty? && !out.include?(dir) &&
      result['status'] == 'preparation_stopped' && result['reason'] == expected_reason &&
      result['temporary_cleanup_failed'] == true && result['installation_ready'] == false &&
      result['device_writes_performed'] == 0 && result['host_files_created'] == (published ? 1 : 0)
    partial = output + '.partial'
    abort 'FAIL: failed cleanup lost source or partial evidence' unless
      File.binread(input) == original && File.file?(partial) && File.size(partial) == 131072
    if published
      abort 'FAIL: published artifact incorrectly accounted' unless File.file?(output) &&
        File.stat(output).ino == File.stat(partial).ino &&
        result['output_sha256'] == Digest::SHA256.file(output).hexdigest
    else
      abort 'FAIL: failed link published an output' if File.exist?(output)
    end
    checks += 1
  end
end
puts "Unlock cleanup: #{checks} real-filesystem before/after publication failures passed"
