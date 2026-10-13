#!/usr/bin/env ruby
# Origin: EmberBSD - guarded persistent factory recovery env prefix transition.
require 'stringio'
require 'shellwords'
require_relative 'a133-recovery-env'
require_relative 'a133-usb-backup-source'

module A133Recovery
  class Protection
    def initialize(adb:,serial:,cid:,root_method:'adbd',timeout:600,require_unlocked:false,additional_guard:nil)
      raise A133Usb::Invalid,'invalid_additional_guard' unless additional_guard.nil? || additional_guard.respond_to?(:call)
      @additional_guard=additional_guard
      raise A133Usb::Invalid,'invalid_protection_options' unless [true,false].include?(require_unlocked)
      @require_unlocked=require_unlocked
      raise A133Usb::Invalid,'invalid_usb_identity' unless serial.is_a?(String) &&
        serial.bytesize.between?(1,128) && serial.match?(/\A[a-zA-Z0-9._-]+\z/) &&
        cid.is_a?(String) && cid.match?(/\A[0-9a-f]{32}\z/)
      @serial,@cid=serial.dup.freeze,cid.dup.freeze
      @root_method=root_method.is_a?(String) ? root_method.dup.freeze : root_method
      adb=adb.is_a?(String) ? adb.dup.freeze : adb
      @source=A133UsbBackup::Source.new(adb:adb,serial:@serial,state:'recovery',
        root_method:@root_method,timeout:timeout)
      @channel=A133Usb::Channel.new(adb,timeout)
    rescue A133Usb::Invalid,A133UsbBackup::Invalid => error
      failure=A133Usb::Invalid.new(error.message)
      failure.write_attempted=false
      raise failure,cause:nil
    end

    def sha?(value)
      value.is_a?(String) && value.match?(/\A[0-9a-f]{64}\z/)
    end

    def policy(original_env,backup,mutable)
      hashes=backup.is_a?(Hash) ? backup['partition_sha256'] : nil
      raise A133Usb::Invalid,'invalid_protection_backup' unless backup.is_a?(Hash) &&
        backup['serial']==@serial && backup['cid']==@cid && backup['status']=='backup_integrity_verified' &&
        backup['bytes']==A133Backup::BYTES && %w[gpt_headers_crc gpt_arrays_crc partition_layout_verified
          critical_copies_verified hardware_boot_copies_verified].all? { |key| backup[key]==true } &&
        sha?(backup['uncompressed_sha256']) && hashes.is_a?(Hash) &&
        hashes.keys.all? { |key| key.is_a?(String) } &&
        hashes.keys.sort==%w[boot bootloader env recovery] && hashes.values.all? { |value| sha?(value) }
      parts=mutable.is_a?(Hash) ? mutable['partition_sha256'] : nil
      raise A133Usb::Invalid,'invalid_protection_mutable' unless mutable.is_a?(Hash) &&
        mutable['serial']==@serial && mutable['cid']==@cid && mutable['status']=='mutable_integrity_verified' &&
        mutable['mutable_copies_verified']==true && sha?(mutable['gpt_sha256']) &&
        mutable['observation']=='recovery_unmounted_two_matching_reads' &&
        mutable['filesystem_consistency']=='not_established_by_integrity_check' &&
        parts.is_a?(Hash) && parts.keys.all? { |key| key.is_a?(String) } &&
        parts.keys.sort==%w[UDISK metadata] && parts.values.all? { |value| sha?(value) }
      env_sha,recovery_sha,gpt_sha=[hashes['env'],hashes['recovery'],mutable['gpt_sha256']].map { |value| value.dup.freeze }
      bytes=A133Backup::INVENTORY.find { |entry| entry[:name]=='env' }.fetch(:sectors)*512
      raise A133Usb::Invalid,'original_environment_size_mismatch' unless original_env.is_a?(String) && original_env.bytesize==bytes
      original=original_env.b.dup.freeze
      raise A133Usb::Invalid,'original_environment_hash_mismatch' unless Digest::SHA256.hexdigest(original)==env_sha
      protected=A133Recovery.protect(original.byteslice(0,A133Env::SIZE)).freeze
      expected=protected+original.byteslice(A133Env::SIZE,bytes-A133Env::SIZE)
      {original_sha:env_sha,expected_sha:Digest::SHA256.hexdigest(expected),
        protected_env:protected,recovery_sha:recovery_sha,gpt_sha:gpt_sha}.freeze
    end

    def read_hash(role)
      @source.mutable_inspect!
      digest=Digest::SHA256.new
      @source.read(role) { |chunk| digest.update(chunk) }
      @source.mutable_inspect!
      digest.hexdigest
    end

    def guard!(policy)
      profile=@source.mutable_inspect!
      raise A133Usb::Invalid,'usb_protection_identity_mismatch' unless profile[:serial]==@serial && profile[:cid]==@cid
      allowed=@require_unlocked ? [%w[0 orange]] : [%w[1 green],%w[0 orange]]
      raise A133Usb::Invalid,'usb_protection_state_unsupported' unless allowed.include?([profile[:locked],profile[:verified]])
      raise A133Usb::Invalid,'usb_protection_gpt_mismatch' unless profile[:gpt_sha256]==policy[:gpt_sha]
      raise A133Usb::Invalid,'usb_recovery_changed' unless read_hash('recovery')==policy[:recovery_sha]
      raise A133Usb::Invalid,'usb_additional_guard_failed' if @additional_guard && @additional_guard.call!=true
    end

    def install(original_env:,backup:,mutable:)
      attempted=false
      pinned=policy(original_env,backup,mutable)
      guard!(pinned)
      current=read_hash('env')
      raise A133Usb::Invalid,'usb_environment_unknown' unless
        [pinned[:original_sha],pinned[:expected_sha]].include?(current)
      guard!(pinned)
      if current==pinned[:original_sha]
        command='dd of=/dev/block/mmcblk0p2 bs=1048576 conv=notrunc 2>/dev/null && sync'
        if @root_method=='vendor_su'
          command="exec /system/xbin/su 0 /system/bin/sh -c #{Shellwords.escape(command)}"
        end
        attempted=true
        result=@channel.run(['-s',@serial,'shell','-T',command],
          source:StringIO.new(pinned[:protected_env]),input_bytes:A133Env::SIZE)
        raise A133Usb::Invalid,'usb_environment_input_changed' unless
          result[:input_sha256]==Digest::SHA256.hexdigest(pinned[:protected_env])
      end
      raise A133Usb::Invalid,'usb_environment_readback_mismatch' unless read_hash('env')==pinned[:expected_sha]
      guard!(pinned)
      {status:'recovery_protection_verified',protected_env:pinned[:protected_env],
        env_sha256:pinned[:expected_sha],writes_performed:attempted ? 1 : 0,installation_ready:false}
    rescue A133Usb::Invalid,A133UsbBackup::Invalid,A133Recovery::Invalid,SystemCallError,IOError => error
      reason=error.is_a?(SystemCallError) || error.is_a?(IOError) ? 'usb_protection_io_failed' : error.message
      failure=A133Usb::Invalid.new(reason)
      failure.write_attempted=attempted
      raise failure,cause:nil
    end
    private :sha?,:policy,:read_hash,:guard!
  end
end
