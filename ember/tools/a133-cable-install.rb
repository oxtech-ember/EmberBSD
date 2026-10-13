#!/usr/bin/env ruby
# Origin: EmberBSD - pinned resumable unlock, recovery protection and image write composition.
require_relative 'a133-cable-journal'
require_relative 'a133-unlock-stage'
require_relative 'a133-recovery-protection'
require_relative 'a133-install-session'

module A133Cable
  module_function
  def reason(error)
    value=[Invalid,A133Unlock::Invalid,A133Recovery::Invalid,A133Usb::Invalid,
      A133UsbBackup::Invalid,BundleError,A133Install::Invalid].any? { |type| error.is_a?(type) } ? error.message : 'cable_operation_failed'
    value.is_a?(String) && value.ascii_only? && value.match?(/\A[a-z0-9_]{1,64}\z/) ? value : 'cable_operation_failed'
  end
  def pin(value)
    case value
    when Hash then value.to_h { |key,item| [pin(key),pin(item)] }.freeze
    when Array then value.map { |item| pin(item) }.freeze
    when String then value.dup.freeze
    else value
    end
  end
  def recovery_guard(store,context,adb,timeout,root_method,env_hashes)
    source=A133UsbBackup::Source.new(adb:adb,serial:context['serial'],state:'recovery',root_method:root_method,timeout:timeout)
    inspect=proc do
      hardware=store.snapshot['hardware_bytes']
      profile=source.mutable_inspect!
      raise A133Usb::Invalid,'cable_recovery_unlock_required' unless profile.values_at(:state,:locked,:verified)==%w[recovery 0 orange]
      raise A133Usb::Invalid,'cable_identity_changed' unless profile[:serial]==context['serial'] &&
        profile[:cid]==context['cid'] && profile[:gpt_sha256]==context['gpt_sha256']
      raise A133Usb::Invalid,'cable_hardware_changed' unless profile[:hardware_bytes]==hardware
    end
    inspect.call
    digest=Digest::SHA256.new
    source.read('env') { |bytes| digest.update(bytes) }
    raise A133Usb::Invalid,'usb_environment_unknown' unless env_hashes.include?(digest.hexdigest)
    inspect.call
    true
  rescue StandardError => error
    raise A133Usb::Invalid.new(reason(error)),cause:nil
  end
  def run(directory:,manifest:,adb:,serial:,cid:,original_env:,backup:,mutable:,
    locked_round_trip_verified:,timeout:600,wait_timeout:120,
    device_root_method:'vendor_su',recovery_root_method:'adbd')
    directory,manifest,adb,serial,cid,original_env,backup,mutable=pin([
      directory,manifest,adb,serial,cid,original_env,backup,mutable])
    raise Invalid,'cable_directory_invalid' unless directory.is_a?(String) && !directory.empty? &&
      directory.encoding.ascii_compatible? && directory.valid_encoding? && !directory.b.include?("\0")
    directory=File.expand_path(directory)
    device_root_method,recovery_root_method=pin([device_root_method,recovery_root_method])
    unlock=A133Unlock::Stage.new(directory:File.join(directory,'unlock'),adb:adb,serial:serial,cid:cid,
      original_env:original_env,backup:backup,mutable:mutable,locked_round_trip_verified:locked_round_trip_verified,
      timeout:timeout,wait_timeout:wait_timeout,device_root_method:device_root_method,recovery_root_method:recovery_root_method)
    base=A133Recovery::Policy.new(serial:serial,cid:cid,original_env:original_env,backup:backup).data
    protected=A133Recovery.protect(base[:original_prefix]).freeze
    protected_sha=Digest::SHA256.hexdigest(protected+original_env.byteslice(131072,16777216-131072)).freeze
    bundle_sha=A133Install.bundle_digest(A133Bundle.verify(manifest)[:receipt]).freeze
    armed=A133Unlock.prepare(base[:original_prefix])
    context={'serial'=>serial,'cid'=>cid,'backup_sha256'=>backup['uncompressed_sha256'],
      'gpt_sha256'=>base[:gpt_sha],'bootloader_sha256'=>base[:bootloader_sha],'boot_sha256'=>base[:boot_sha],
      'recovery_sha256'=>base[:recovery_sha],'original_env_sha256'=>base[:original_sha],
      'unlock_env_sha256'=>Digest::SHA256.hexdigest(armed+original_env.byteslice(131072,16777216-131072)),
      'mutable_sha256'=>Journal.digest(mutable['partition_sha256']),
      'protected_env_sha256'=>protected_sha,'bundle_sha256'=>bundle_sha}
    latest=nil
    Journal.open(directory,context) do |store|
      begin
        latest=store.report
        unless store.snapshot['protection_started']
          store.record('unlocking',write:true,reboot:true)
          latest=store.report
          result=unlock.execute
          store.record('unlocked',unlocked:true,hardware_bytes:result.fetch(:hardware_bytes))
          latest=store.report
          store.record('protecting',protection:true)
          latest=store.report
        end
        protection_hashes=[base[:original_sha],protected_sha].freeze
        image_hashes=[protected_sha].freeze
        preparation_guard=proc { recovery_guard(store,context,adb,timeout,recovery_root_method,protection_hashes) }
        image_guard=proc { recovery_guard(store,context,adb,timeout,recovery_root_method,image_hashes) }
        preparation_guard.call
        store.record('protecting')
        latest=store.report
        protection=A133Recovery::Protection.new(adb:adb,serial:serial,cid:cid,root_method:recovery_root_method,
          timeout:timeout,require_unlocked:true,additional_guard:preparation_guard).install(original_env:original_env,backup:backup,mutable:mutable)
        raise Invalid,'cable_protection_changed' unless protection[:protected_env]==protected && protection[:env_sha256]==protected_sha
        store.record('protected')
        latest=store.report
        store.record('writing')
        latest=store.report
        images=A133Install.run(directory:File.join(directory,'images'),manifest:manifest,adb:adb,serial:serial,cid:cid,
          backup:backup,protected_env:protected,timeout:timeout,expected_bundle_sha256:bundle_sha,additional_guard:image_guard)
        store.record('verified')
        store.report.merge(status:'cable_write_stage_verified',image_writes_performed:images[:writes_performed],
          protection_writes_performed:protection[:writes_performed],roles:images[:roles])
      rescue StandardError => error
        value=reason(error)
        begin
          store.record('failed',reason:value)
          latest=store.report
        rescue StandardError
          # Earlier intent remains the conservative authority if persistence fails.
        end
        report=(latest || {}).merge(status:'cable_write_stage_stopped',installation_ready:false)
        report=report.merge(effects_unknown:true,possible_write:true,possible_reboot:true) if value.start_with?('cable_journal_')
        raise Invalid.new(value,report),cause:nil
      end
    end
  rescue Invalid => error
    raise if error.report[:status]=='cable_write_stage_stopped'
    raise Invalid.new(reason(error),{status:'cable_write_stage_stopped',effects_unknown:true,
      possible_write:true,possible_reboot:true,installation_ready:false}),cause:nil
  rescue StandardError => error
    raise Invalid.new(reason(error),{status:'cable_write_stage_stopped',effects_unknown:true,
      possible_write:true,possible_reboot:true,installation_ready:false}),cause:nil
  end
  private_class_method :pin,:recovery_guard
end

if $PROGRAM_NAME==__FILE__
  begin
    raise A133Cable::Invalid,'invalid_arguments' unless ARGV.size==1
    report=nil
    A133Cable::Journal.open(ARGV.first,nil) { |store| report=store.report }
    puts JSON.pretty_generate(report)
  rescue StandardError => error
    puts JSON.pretty_generate(status:'cable_inspection_stopped',reason:A133Cable.reason(error),installation_ready:false)
    exit 1
  end
end
