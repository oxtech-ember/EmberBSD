#!/usr/bin/env ruby
# Origin: EmberBSD - prepare a factory recovery env copy without device access.
require 'digest'
require 'json'
require_relative 'a133-env-edit'

module A133Recovery
  class Invalid < StandardError; end
  PROFILE = {
    'bootcmd' => 'run setargs_mmc boot_normal',
    'boot_normal' => 'run ${hook};run boot_android',
    'boot_android' => 'sunxi_flash read 45000000 boot;bootm 45000000',
    'boot_recovery' => 'sunxi_flash read 45000000 recovery;bootm 45000000'
  }.freeze
  UPDATES = {
    'hook' => 'ember_restore_normal ember_recovery_once',
    'ember_restore_normal' => 'setenv hook; saveenv',
    'ember_recovery_once' => 'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'
  }.freeze
  TRIAL_HELPERS = {
    'ember_trial_rearm' => 'setenv boot_normal run ember_recovery_once',
    'ember_trial_save' => 'saveenv'
  }.transform_values(&:freeze).freeze
  module_function

  def prepare(data)
    vars = A133Env.decode(data).fetch(:entries).to_h
    raise Invalid, 'recovery_hook_already_present' if UPDATES.keys.any? { |key| vars.key?(key) }
    raise Invalid, 'unsupported_factory_environment' unless PROFILE.all? { |key, value| vars[key] == value }
    candidate = A133Env.patch(data, UPDATES)
    restored = A133Env.patch(candidate, UPDATES.transform_values { '' })
    raise Invalid, 'non_reversible_environment' unless restored == data
    candidate
  rescue A133Env::Invalid
    raise Invalid, 'invalid_environment'
  end

  def protect(data)
    vars = A133Env.decode(data).fetch(:entries).to_h
    raise Invalid, 'recovery_hook_already_present' if UPDATES.keys.any? { |key| vars.key?(key) }
    raise Invalid, 'unsupported_factory_environment' unless PROFILE.all? { |key, value| vars[key] == value }
    candidate = A133Env.patch(data, {
      'boot_normal' => 'run ember_recovery_once',
      'ember_recovery_once' => UPDATES.fetch('ember_recovery_once')
    })
    restored = A133Env.patch(candidate, {
      'boot_normal' => PROFILE.fetch('boot_normal'), 'ember_recovery_once' => ''
    })
    raise Invalid, 'non_reversible_environment' unless restored == data
    candidate
  rescue A133Env::Invalid
    raise Invalid, 'invalid_environment', cause: nil
  end

  # A single run list stops before boot_android if rearming or saveenv fails.
  # The saved state retains both inert helpers but always selects recovery.
  # These copied bytes do not establish vendor execution or first-boot acceptance.
  def trial(data)
    raise Invalid, 'invalid_environment', cause: nil unless data.is_a?(String) && data.bytesize == A133Env::SIZE
    original = data.b.dup.freeze
    vars = A133Env.decode(original).fetch(:entries).to_h
    raise Invalid, 'trial_hook_already_present', cause: nil if TRIAL_HELPERS.keys.any? { |key| vars.key?(key) }
    protected = protect(original)
    rearmed = A133Env.patch(protected, TRIAL_HELPERS)
    candidate = A133Env.patch(rearmed, {
      'boot_normal' => 'run ember_trial_rearm ember_trial_save boot_android'
    })
    restored = A133Env.patch(candidate, {
      'boot_normal' => PROFILE.fetch('boot_normal'), 'ember_recovery_once' => '',
      'ember_trial_rearm' => '', 'ember_trial_save' => ''
    })
    raise Invalid, 'non_reversible_environment', cause: nil unless restored == original
    {trial_env: candidate.freeze, rearmed_env: rearmed.freeze, installation_ready: false}.freeze
  rescue A133Env::Invalid
    raise Invalid, 'invalid_environment', cause: nil
  rescue Invalid => error
    reason = error.message
    reason = 'invalid_environment' unless reason.ascii_only? && reason.match?(/\A[a-z0-9_]{1,64}\z/)
    raise Invalid, reason, cause: nil
  end
end

if $PROGRAM_NAME == __FILE__
  result = {device_writes_performed: 0, installation_ready: false}
  partial_stat = nil
  partial = nil
  begin
    raise A133Recovery::Invalid, 'invalid_arguments' unless ARGV.size == 2
    input, output = ARGV
    partial = output + '.partial'
    raise A133Recovery::Invalid, 'output_path_unavailable' if
      File.exist?(output) || File.symlink?(output) || File.exist?(partial) || File.symlink?(partial)
    raise A133Recovery::Invalid, 'input_not_regular' unless File.lstat(input).file?
    data = File.open(input, File::RDONLY | File::NOFOLLOW | File::NONBLOCK) do |file|
      before = file.stat
      raise A133Recovery::Invalid, 'input_not_regular' unless before.file?
      raise A133Recovery::Invalid, 'invalid_environment' unless before.size == A133Env::SIZE
      bytes = file.read(A133Env::SIZE + 1)
      after = file.stat
      raise A133Recovery::Invalid, 'input_changed' unless [:dev, :ino, :size, :mtime, :ctime].all? do |field|
        before.public_send(field) == after.public_send(field)
      end
      raise A133Recovery::Invalid, 'invalid_environment' unless bytes && bytes.bytesize == A133Env::SIZE
      bytes
    end
    candidate = A133Recovery.prepare(data)
    File.open(partial, File::WRONLY | File::CREAT | File::EXCL, 0600) do |file|
      partial_stat = file.stat
      file.write(candidate)
      file.flush
      file.fsync
    end
    # link is atomic and refuses an output created since the initial check.
    File.link(partial, output)
    result[:status] = 'recovery_copy_prepared'
    result[:input_sha256] = Digest::SHA256.hexdigest(data)
    result[:output_sha256] = Digest::SHA256.hexdigest(candidate)
    result[:changed_variables] = A133Recovery::UPDATES.keys.sort
    result[:host_files_created] = 1
    puts JSON.pretty_generate(result)
  rescue A133Recovery::Invalid, SystemCallError, IOError => error
    result[:status] = 'preparation_stopped'
    result[:reason] = error.is_a?(A133Recovery::Invalid) ? error.message : 'host_file_error'
    puts JSON.pretty_generate(result)
    exit 1
  ensure
    if partial_stat && partial
      begin
        current = File.lstat(partial)
        File.unlink(partial) if current.dev == partial_stat.dev && current.ino == partial_stat.ino
      rescue Errno::ENOENT
        # The owned partial was already removed; never remove another inode.
      end
    end
  end
end
