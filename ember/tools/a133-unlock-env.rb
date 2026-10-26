#!/usr/bin/env ruby
# Origin: EmberBSD - prepare a reversible vendor unlock hook on a copied env only.
require 'digest'
require 'json'
require_relative 'a133-env-edit'
require_relative 'a133-recovery-env'

module A133Unlock
  class Invalid < StandardError; end
  PROFILE = {
    'bootcmd' => 'run setargs_mmc boot_normal',
    'boot_normal' => 'run ${hook};run boot_android',
    'boot_android' => 'sunxi_flash read 45000000 boot;bootm 45000000',
    'boot_recovery' => 'sunxi_flash read 45000000 recovery;bootm 45000000'
  }.transform_values(&:freeze).freeze
  UPDATES = {
    'hook' => 'ember_unlock_flag ember_unlock_next',
    'ember_unlock_flag' => 'pst write fastboot_status_flag unlocked',
    'ember_unlock_next' => 'setenv hook ember_restore_normal ember_recovery_once; run ember_save_env ember_reset',
    'ember_save_env' => 'saveenv',
    'ember_reset' => 'reset',
    'ember_restore_normal' => 'setenv hook; saveenv',
    'ember_recovery_once' => 'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'
  }.transform_values(&:freeze).freeze
  module_function

  def plan(data)
    raise Invalid, 'invalid_environment' unless data.is_a?(String) && data.bytesize == A133Env::SIZE
    original = data.b.dup.freeze
    vars = A133Env.decode(original).fetch(:entries).to_h
    raise Invalid, 'unlock_hook_already_present' if UPDATES.keys.any? { |key| vars.key?(key) }
    if vars['bootcmd']==A133Recovery::DIRECT_PROFILE.fetch('bootcmd')
      entry=A133Recovery.entry_plan(original)
      recovery_command=A133Env.decode(entry.fetch(:armed_env)).fetch(:entries).to_h.fetch('bootcmd')
      helpers={'ember_unlock_flag'=>UPDATES.fetch('ember_unlock_flag'),
        'ember_unlock_next'=>"setenv bootcmd #{recovery_command}; run ember_entry_save ember_reset",
        'ember_reset'=>UPDATES.fetch('ember_reset')}
      updates=A133Recovery::DIRECT_HELPERS.merge(helpers).merge('bootcmd'=>'run ember_unlock_flag ember_unlock_next')
      reverse=updates.transform_values { '' }.merge('bootcmd'=>vars.fetch('bootcmd'))
      consumed=entry.fetch(:consumed).merge(helpers)
    else
      raise Invalid, 'unsupported_factory_environment' unless PROFILE.all? { |key, value| vars[key] == value }
      updates=UPDATES
      reverse=updates.transform_values { '' }
      consumed=vars.merge(UPDATES.reject { |key,_| key=='hook' })
    end
    candidate = A133Env.patch(original, updates)
    restored = A133Env.patch(candidate, reverse)
    raise Invalid, 'non_reversible_environment' unless restored == original
    consumed=consumed.to_h { |key,value| [key.dup.freeze,value.dup.freeze] }.freeze
    {armed_env:candidate.freeze,consumed:consumed,changed_variables:updates.keys.sort.map(&:freeze).freeze}.freeze
  rescue A133Env::Invalid
    raise Invalid, 'invalid_environment', cause: nil
  rescue A133Recovery::Invalid => error
    reason=error.message=='recovery_hook_already_present' ? 'unlock_hook_already_present' : error.message
    raise Invalid,reason,cause:nil
  end

  def prepare(data)
    plan(data).fetch(:armed_env)
  end
end

if $PROGRAM_NAME == __FILE__
  result = {device_writes_performed: 0, installation_ready: false, host_files_created: 0}
  partial_stat = nil
  partial = nil
  begin
    raise A133Unlock::Invalid, 'invalid_arguments' unless ARGV.size == 2 && ARGV.all? do |arg|
      arg.encoding.ascii_compatible? && arg.valid_encoding? && !arg.b.include?("\0")
    end
    input, output = ARGV
    partial = output + '.partial'
    raise A133Unlock::Invalid, 'output_path_unavailable' if
      [output, partial].any? { |path| File.exist?(path) || File.symlink?(path) }
    raise A133Unlock::Invalid, 'input_not_regular' unless File.lstat(input).file?
    data = File.open(input, File::RDONLY | File::NOFOLLOW | File::NONBLOCK) do |file|
      before = file.stat
      raise A133Unlock::Invalid, 'input_not_regular' unless before.file?
      raise A133Unlock::Invalid, 'invalid_environment' unless before.size == A133Env::SIZE
      bytes = file.read(A133Env::SIZE + 1)
      after = file.stat
      raise A133Unlock::Invalid, 'input_changed' unless [:dev, :ino, :size, :mtime, :ctime].all? do |field|
        before.public_send(field) == after.public_send(field)
      end
      raise A133Unlock::Invalid, 'invalid_environment' unless bytes && bytes.bytesize == A133Env::SIZE
      bytes
    end
    plan = A133Unlock.plan(data)
    candidate = plan.fetch(:armed_env)
    File.open(partial, File::WRONLY | File::CREAT | File::EXCL, 0600) do |file|
      partial_stat = file.stat
      file.write(candidate)
      file.flush
      file.fsync
    end
    File.link(partial, output)
    result[:host_files_created] = 1
    result[:status] = 'unlock_copy_prepared'
    result[:input_sha256] = Digest::SHA256.hexdigest(data)
    result[:output_sha256] = Digest::SHA256.hexdigest(candidate)
    result[:changed_variables] = plan.fetch(:changed_variables)
  rescue A133Unlock::Invalid, SystemCallError, IOError => error
    result[:status] = 'preparation_stopped'
    result[:reason] = error.is_a?(A133Unlock::Invalid) ? error.message : 'host_file_error'
  ensure
    if partial_stat && partial
      begin
        current = File.lstat(partial)
        File.unlink(partial) if current.dev == partial_stat.dev && current.ino == partial_stat.ino
      rescue Errno::ENOENT
        # Remove only our owned temporary inode, never a replaced path.
      rescue SystemCallError, IOError
        result[:temporary_cleanup_failed] = true
        if result[:status] == 'unlock_copy_prepared'
          result[:status] = 'preparation_stopped'
          result[:reason] = 'host_cleanup_failed'
        end
      end
    end
  end
  puts JSON.pretty_generate(result)
  exit 1 if result[:status] == 'preparation_stopped'
end
