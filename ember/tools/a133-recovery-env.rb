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
  DIRECT_PROFILE = {
    'bootcmd'=>'run setargs_nand boot_normal',
    'boot_normal'=>'sunxi_flash read 45000000 boot;bootm 45000000',
    'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000'
  }.transform_values(&:freeze).freeze
  # Protection executes only this known factory command, not arbitrary helpers.
  DIRECT_SETARGS = 'setenv bootargs earlyprintk=${earlyprintk} initcall_debug=${initcall_debug} ' \
    'console=${console} loglevel=${loglevel} root=${nand_root} init=${init} partitions=${partitions} ' \
    'cma=${cma} snum=${snum} mac_addr=${mac} wifi_mac=${wifi_mac} bt_mac=${bt_mac} ' \
    'specialstr=${specialstr} gpt=1'
  DIRECT_SETARGS.freeze
  DIRECT_HELPERS = {
    'ember_entry_restore'=>'setenv bootcmd run setargs_nand boot_normal',
    'ember_entry_save'=>'saveenv',
    'ember_entry_fdt'=>'fdt addr ${fdtcontroladdr}',
    'ember_entry_usb'=>'fdt set /soc@03000000/usbc0@0 usb_port_type <0>'
  }.transform_values(&:freeze).freeze
  PROTECTION_UPDATES = {
    'boot_normal' => 'run ember_recovery_once',
    'ember_recovery_once' => UPDATES.fetch('ember_recovery_once').dup
  }.transform_values(&:freeze).freeze
  DIRECT_PROTECTION_UPDATES = DIRECT_HELPERS.slice('ember_entry_fdt', 'ember_entry_usb').merge(
    'ember_recovery_once' => 'run ember_entry_fdt ember_entry_usb boot_recovery',
    'bootcmd' => 'run setargs_nand ember_recovery_once'
  ).transform_values(&:freeze).freeze
  DIRECT_TRIAL_HELPERS = TRIAL_HELPERS.merge(
    'ember_trial_rearm' => 'setenv bootcmd run setargs_nand ember_recovery_once',
    'ember_trial_load' => 'sunxi_flash read 45000000 boot',
    'ember_trial_boot' => 'bootm 45000000'
  ).transform_values(&:freeze).freeze
  module_function

  # Factory entry restores its original boot selection before recovery.
  def entry_plan(data)
    vars=A133Env.decode(data).fetch(:entries).to_h
    if vars['bootcmd']==DIRECT_PROFILE.fetch('bootcmd')
      reserved=DIRECT_HELPERS.keys+%w[hook boot_android ember_restore_normal ember_recovery_once]
      raise Invalid,'recovery_hook_already_present' if reserved.any? { |key| vars.key?(key) }
      raise Invalid,'unsupported_factory_environment' unless DIRECT_PROFILE.all? { |key,value| vars[key]==value } &&
        vars['setargs_nand'].is_a?(String) && !vars['setargs_nand'].empty?
      updates=DIRECT_HELPERS.merge('bootcmd'=>'run ember_entry_restore ember_entry_save setargs_nand ember_entry_fdt ember_entry_usb boot_recovery')
      armed=A133Env.patch(data,updates)
      reverse=DIRECT_HELPERS.transform_values { '' }.merge('bootcmd'=>DIRECT_PROFILE.fetch('bootcmd'))
      raise Invalid,'non_reversible_environment' unless A133Env.patch(armed,reverse)==data
      consumed=vars.merge(DIRECT_HELPERS)
      profile='direct'
    else
      armed=prepare(data)
      consumed=vars.merge(UPDATES.reject { |key,_| key=='hook' })
      profile='hook'
    end
    consumed=consumed.to_h { |key,value| [key.dup.freeze,value.dup.freeze] }.freeze
    {profile:profile.freeze,armed_env:armed.freeze,consumed:consumed,installation_ready:false}.freeze
  rescue A133Env::Invalid
    raise Invalid,'invalid_environment',cause:nil
  rescue Invalid => error
    raise Invalid,error.message,cause:nil
  end

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
    if vars['bootcmd'] == DIRECT_PROFILE.fetch('bootcmd')
      raise Invalid, 'recovery_hook_already_present' if DIRECT_HELPERS.keys.any? { |key| vars.key?(key) }
      raise Invalid, 'unsupported_factory_environment' unless DIRECT_PROFILE.all? { |key, value| vars[key] == value } &&
        vars['setargs_nand'] == DIRECT_SETARGS
      updates = DIRECT_PROTECTION_UPDATES
    else
      raise Invalid, 'unsupported_factory_environment' unless PROFILE.all? { |key, value| vars[key] == value }
      updates = PROTECTION_UPDATES
    end
    candidate = A133Env.patch(data, updates)
    restored = A133Env.patch(candidate, updates.to_h { |key, _| [key, vars.fetch(key, '')] })
    raise Invalid, 'non_reversible_environment' unless restored == data
    candidate
  rescue A133Env::Invalid
    raise Invalid, 'invalid_environment', cause: nil
  end

  # Shared byte-profile check for image writers. This does not inspect a device.
  def protected_profile_model(vars)
    return nil unless vars.is_a?(Hash)
    hook = PROFILE.merge(PROTECTION_UPDATES)
    return 'a133' if hook.all? { |key, value| vars[key] == value }
    return 'QUAD-CORE A133 c3' if DIRECT_PROFILE.merge(DIRECT_PROTECTION_UPDATES).all? do |key, value|
      vars[key] == value
    end && vars['setargs_nand'] == DIRECT_SETARGS
    nil
  end

  def protected_profile?(vars)
    !protected_profile_model(vars).nil?
  end

  # A single run list stops before kernel loading if rearming or saveenv fails.
  # The saved state retains inert helpers but always selects recovery.
  # These copied bytes do not establish vendor execution or first-boot acceptance.
  def trial(data)
    raise Invalid, 'invalid_environment', cause: nil unless data.is_a?(String) && data.bytesize == A133Env::SIZE
    original = data.b.dup.freeze
    vars = A133Env.decode(original).fetch(:entries).to_h
    direct = vars['bootcmd'] == DIRECT_PROFILE.fetch('bootcmd')
    helpers = direct ? DIRECT_TRIAL_HELPERS : TRIAL_HELPERS
    raise Invalid, 'trial_hook_already_present', cause: nil if helpers.keys.any? { |key| vars.key?(key) }
    protected = protect(original)
    rearmed = A133Env.patch(protected, helpers)
    boot = direct ? {'bootcmd' => 'run ember_trial_rearm ember_trial_save setargs_nand ember_trial_load ember_trial_boot'} :
      {'boot_normal' => 'run ember_trial_rearm ember_trial_save boot_android'}
    candidate = A133Env.patch(rearmed, boot)
    changed = helpers.keys + (direct ? DIRECT_PROTECTION_UPDATES.keys : PROTECTION_UPDATES.keys)
    restored = A133Env.patch(candidate, changed.to_h { |key| [key, vars.fetch(key, '')] })
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
