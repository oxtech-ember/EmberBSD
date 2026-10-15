#!/usr/bin/env ruby
# Origin: EmberBSD - guarded locked-Android recovery entry and factory env restoration.
require 'stringio'
require 'shellwords'
require_relative 'a133-recovery-policy'
require_relative 'a133-usb-backup-source'

module A133Recovery
  class Entry
    def initialize(adb:,serial:,cid:,state:,root_method:'vendor_su',timeout:600)
      raise A133Usb::Invalid,'invalid_usb_identity' unless serial.is_a?(String) && serial.ascii_only? &&
        serial.bytesize.between?(1,128) && serial.match?(/\A[a-zA-Z0-9._-]+\z/) &&
        cid.is_a?(String) && cid.ascii_only? && cid.match?(/\A[0-9a-f]{32}\z/)
      @serial,@cid=serial.dup.freeze,cid.dup.freeze
      @state=state.is_a?(String) ? state.dup.freeze : state
      @root_method=root_method.is_a?(String) ? root_method.dup.freeze : root_method
      adb=adb.is_a?(String) ? adb.dup.freeze : adb
      @source=A133UsbBackup::Source.new(adb:adb,serial:@serial,state:@state,
        root_method:@root_method,timeout:timeout)
      @channel=A133Usb::Channel.new(adb,timeout)
    rescue A133Usb::Invalid,A133UsbBackup::Invalid => error
      failure=A133Usb::Invalid.new(error.message)
      failure.write_attempted=false
      raise failure,cause:nil
    end

    def policy(original_env,backup)
      A133Recovery::Policy.new(serial:@serial,cid:@cid,original_env:original_env,backup:backup).data
    end

    def read_hash(role)
      @source.environment_inspect!
      digest=Digest::SHA256.new
      @source.read(role) { |chunk| digest.update(chunk) }
      @source.environment_inspect!
      digest.hexdigest
    end

    def read_environment
      @source.environment_inspect!
      digest,tail=Digest::SHA256.new,Digest::SHA256.new
      prefix=''.b; offset=0
      @source.read('env') do |chunk|
        digest.update(chunk)
        prefix << chunk.byteslice(0,[A133Env::SIZE-prefix.bytesize,chunk.bytesize].min) if prefix.bytesize<A133Env::SIZE
        skip=[A133Env::SIZE-offset,0].max
        tail.update(chunk.byteslice(skip,chunk.bytesize-skip)) if skip<chunk.bytesize
        offset+=chunk.bytesize
      end
      @source.environment_inspect!
      {sha:digest.hexdigest,prefix:prefix,tail_sha:tail.hexdigest}
    end

    def guard!(policy)
      profile=@source.environment_inspect!
      raise A133Usb::Invalid,'usb_entry_identity_mismatch' unless profile[:serial]==@serial && profile[:cid]==@cid
      raise A133Usb::Invalid,'usb_entry_state_unsupported' unless profile[:locked]=='1' && profile[:verified]=='green'
      raise A133Usb::Invalid,'usb_entry_gpt_mismatch' unless profile[:gpt_sha256]==policy[:gpt_sha]
      raise A133Usb::Invalid,'usb_bootloader_changed' unless read_hash('bootloader')==policy[:bootloader_sha]
      raise A133Usb::Invalid,'usb_recovery_changed' unless read_hash('recovery')==policy[:recovery_sha]
    end

    def consumed?(current,policy)
      current[:tail_sha]==policy[:tail_sha] && A133Env.decode(current[:prefix]).fetch(:entries).to_h==policy[:consumed]
    rescue A133Env::Invalid
      false
    end

    def transition(action,original_env,backup)
      attempted=false
      required=action==:arm ? 'device' : 'recovery'
      raise A133Usb::Invalid,'usb_entry_wrong_state' unless @state==required
      pinned=policy(original_env,backup)
      guard!(pinned)
      current=read_environment
      known=[pinned[:original_sha],pinned[:armed_sha]].include?(current[:sha])
      known ||= action==:restore && consumed?(current,pinned)
      raise A133Usb::Invalid,'usb_environment_unknown' unless known
      prefix=action==:arm ? pinned[:armed_prefix] : pinned[:original_prefix]
      expected=action==:arm ? pinned[:armed_sha] : pinned[:original_sha]
      guard!(pinned)
      if current[:sha]!=expected
        command='dd of=/dev/block/mmcblk0p2 bs=1048576 conv=notrunc 2>/dev/null && sync'
        if @root_method=='vendor_su'
          command="exec /system/xbin/su 0 /system/bin/sh -c #{Shellwords.escape(command)}"
        end
        attempted=true
        result=@channel.run(['-s',@serial,'shell','-T',command],source:StringIO.new(prefix),input_bytes:A133Env::SIZE)
        raise A133Usb::Invalid,'usb_environment_input_changed' unless result[:input_sha256]==Digest::SHA256.hexdigest(prefix)
      end
      raise A133Usb::Invalid,'usb_environment_readback_mismatch' unless read_environment[:sha]==expected
      guard!(pinned)
      {status:action==:arm ? 'env_entry_armed' : 'env_entry_restored',env_sha256:expected,
        writes_performed:attempted ? 1 : 0,installation_ready:false}
    rescue A133Usb::Invalid,A133UsbBackup::Invalid,A133Recovery::Invalid,SystemCallError,IOError => error
      reason=error.is_a?(SystemCallError) || error.is_a?(IOError) ? 'usb_entry_io_failed' : error.message
      failure=A133Usb::Invalid.new(reason)
      failure.write_attempted=attempted
      raise failure,cause:nil
    end

    def arm(original_env:,backup:)
      transition(:arm,original_env,backup)
    end

    def restore(original_env:,backup:)
      transition(:restore,original_env,backup)
    end
    private :policy,:read_hash,:read_environment,:guard!,:consumed?,:transition
  end
end
