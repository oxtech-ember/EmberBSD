#!/usr/bin/env ruby
# Origin: EmberBSD - native acceptance and normal release in the cable owner.
require_relative 'a133-cable-install'

module A133Cable
  module_function

  def native_image_binding!(saved,images,policy)
    raise Invalid,'native_cable_not_verified',cause:nil unless saved['phase']=='verified' &&
      saved['unlock_verified'] && saved['protection_started']
    context=saved.fetch('context');base=policy.data
    expected={'serial'=>base[:serial],'cid'=>base[:cid],'gpt_sha256'=>base[:gpt_sha],
      'bootloader_sha256'=>base[:bootloader_sha],'boot_sha256'=>base[:boot_sha],
      'recovery_sha256'=>base[:recovery_sha],'original_env_sha256'=>base[:original_sha]}
    raise Invalid,'native_policy_changed',cause:nil unless expected.all? { |key,value| context[key]==value }
    raise Invalid,'native_images_not_verified',cause:nil unless images['roles'].values.all? { |row| row['state']=='verified' }
    image=images.fetch('context')
    raise Invalid,'native_image_context_changed',cause:nil unless %w[serial cid backup_sha256 recovery_sha256].all? { |key| image[key]==context[key] } &&
      image['protected_env_sha256']==Digest::SHA256.hexdigest(A133Recovery.protect(base[:original_prefix]))
    receipt={writes_performed:0,installation_ready:false,status:'artifact_manifest_verified',
      source_commit:image['source_commit'],artifacts:image['artifacts'].map do |row|
        name={'root'=>'UDISK','boot'=>'boot','resources'=>'bootloader'}.fetch(row['role'])
        row.merge('start_sector'=>A133Backup::INVENTORY.find { |part| part[:name]==name }.fetch(:start))
      end}
    raise Invalid,'native_bundle_changed',cause:nil unless A133Install.bundle_digest(receipt)==context['bundle_sha256']
    image['artifacts'].find { |row| row['role']=='boot' }.fetch('sha256')
  end

  def native_save_environment!(directory,store,bytes)
    store.snapshot
    path=File.join(directory,'native-env-before.bin')
    if File.exist?(path) || File.symlink?(path)
      Journal.secure!(File.lstat(path))
      File.open(path,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |file|
        Journal.secure!(file.stat)
        raise Invalid,'native_saved_environment_changed',cause:nil unless file.stat.size==bytes.bytesize && file.read(bytes.bytesize+1)==bytes
      end
    else
      File.open(path,File::WRONLY|File::CREAT|File::EXCL|File::NOFOLLOW,0600) do |file|
        file.write(bytes);file.flush;file.fsync
      end
      File.open(directory,File::RDONLY|File::NOFOLLOW) { |file| file.fsync }
    end
    store.snapshot
  end

  # The source implements the native transport contract. Acceptance belongs to
  # the caller; this OS stage does not certify an application's user interface.
  def native_postboot(directory:,policy:,source:,nonce:,acceptance:,release:false)
    latest=nil
    raise Invalid,'native_options_invalid',cause:nil unless policy.is_a?(A133Recovery::Policy) &&
      nonce.is_a?(String) && nonce.match?(/\A[0-9a-f]{48}\z/) && acceptance.respond_to?(:call) && [true,false].include?(release)
    directory=File.expand_path(directory);nonce=nonce.dup.freeze
    Journal.open(directory,nil) do |store|
      A133Install::Store.open(File.join(directory,'images'),nil) do |images|
        saved=store.snapshot
        latest=store.report
        boot_sha=native_image_binding!(saved,images.snapshot,policy)
        key_sha=source.host_key_sha256
        version_sha=source.kernel_version_sha256
        raise Invalid,'native_host_key_invalid',cause:nil unless A133Install.sha?(key_sha)
        raise Invalid,'native_kernel_version_invalid',cause:nil unless A133Install.sha?(version_sha)
        observed=source.observe(policy:policy,boot_sha256:boot_sha,nonce:nonce)
        epoch=observed.is_a?(Hash) ? observed[:epoch] : nil
        raise Invalid,'native_observation_invalid',cause:nil unless observed.is_a?(Hash) && observed.keys.all? { |key| key.is_a?(Symbol) } &&
          observed.keys.sort==[:epoch,:marker] &&
          epoch.is_a?(String) && epoch.bytesize.between?(1,512) && epoch.ascii_only? && !epoch.include?("\0") &&
          (observed[:marker].nil? || (observed[:marker].is_a?(String) && observed[:marker].match?(/\A[0-9a-f]{48}\z/)))
        epoch=epoch.dup.freeze
        binding={'host_key_sha256'=>key_sha,'kernel_version_sha256'=>version_sha,'nonce_sha256'=>Digest::SHA256.hexdigest(nonce),
          'boot_sha256'=>boot_sha,'epoch_sha256'=>Digest::SHA256.hexdigest(epoch)}
        native=saved['native']
        raise Invalid,'native_confirmation_marker_changed',cause:nil if observed[:marker] && observed[:marker]!=nonce
        if native
          raise Invalid,'native_boot_epoch_changed',cause:nil unless native['epoch_sha256']==binding['epoch_sha256']
          raise Invalid,'native_confirmation_context_changed',cause:nil unless binding.all? { |key,value| native[key]==value }
        else
          raise Invalid,'native_confirmation_marker_changed',cause:nil unless observed[:marker]==nonce
          actual=source.environment
          normal=policy.normal_boot_env(actual)
          raise Invalid,'native_acceptance_required',cause:nil unless acceptance.call==true
          native_save_environment!(directory,store,actual)
          native=binding.merge('phase'=>'confirming','before_env_sha256'=>Digest::SHA256.hexdigest(actual),
            'normal_env_sha256'=>Digest::SHA256.hexdigest(normal))
          store.record_native(native)
          latest=store.report
        end
        if native['phase']=='confirming'
          if observed[:marker]==nonce
            raise Invalid,'native_confirmation_failed',cause:nil unless source.confirm(nonce:nonce,epoch:epoch)==true
          end
          native=native.merge('phase'=>'confirmed');store.record_native(native)
          latest=store.report
        elsif observed[:marker]
          raise Invalid,'native_confirmation_marker_changed',cause:nil
        end
        if release
          actual=source.environment
          digest=Digest::SHA256.hexdigest(actual)
          if %w[releasing released].include?(native['phase'])
            # An ambiguous write is observed once. It is never submitted again.
            raise Invalid,'native_release_observe_only',cause:nil unless digest==native['normal_env_sha256']
          else
            raise Invalid,'native_environment_changed',cause:nil unless digest==native['before_env_sha256']
            normal=policy.normal_boot_env(actual)
            raise Invalid,'native_environment_changed',cause:nil unless Digest::SHA256.hexdigest(normal)==native['normal_env_sha256']
            native=native.merge('phase'=>'releasing');store.record_native(native)
            latest=store.report
            source.write_environment(prefix:normal.byteslice(0,A133Env::SIZE),epoch:epoch)
            raise Invalid,'native_environment_readback_failed',cause:nil unless source.environment==normal
          end
          native=native.merge('phase'=>'released');store.record_native(native)
          latest=store.report
        end
        {status:'native_postboot_verified',native_confirmed:true,normal_boot_released:native['phase']=='released',
          reboot_requested:false,per_boot_auto_recovery:false,installation_ready:false}
      end
    end
  rescue StandardError => error
    value=reason(error)
    value='native_postboot_io_failed' if error.is_a?(SystemCallError) || error.is_a?(IOError)
    report=(latest || {}).merge(status:'native_postboot_stopped',installation_ready:false)
    report=report.merge(effects_unknown:true) if latest.nil? || value.start_with?('cable_journal_')
    raise Invalid.new(value,report),cause:nil
  end
  private_class_method :native_image_binding!,:native_save_environment!
end
