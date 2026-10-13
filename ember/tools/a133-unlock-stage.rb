#!/usr/bin/env ruby
# Origin: EmberBSD - journaled fixed-profile vendor unlock and verified recovery return.
require 'stringio'
require 'shellwords'
require_relative 'a133-unlock-journal'
require_relative 'a133-recovery-policy'
require_relative 'a133-usb-backup-source'

module A133Unlock
  class Stage
    class Invalid < A133Unlock::Invalid
      attr_reader :report
      def initialize(reason,report)
        super(reason)
        @report=report
      end
    end
    def self.run(**options)
      new(**options).execute
    rescue Invalid
      raise
    rescue StandardError => error
      raise Invalid.new(reason(error),{status:'unlock_stage_stopped',possible_write:true,
        possible_reboot:true,effects_unknown:true,installation_ready:false}),cause:nil
    end
    def self.reason(error)
      value=error.is_a?(A133Unlock::Invalid) || error.is_a?(A133Usb::Invalid) || error.is_a?(A133UsbBackup::Invalid) ||
        error.is_a?(A133Recovery::Invalid) ? error.message : 'unlock_operation_failed'
      value.is_a?(String) && value.ascii_only? && value.match?(/\A[a-z0-9_]{1,64}\z/) ? value : 'unlock_operation_failed'
    end
    def initialize(directory:,adb:,serial:,cid:,original_env:,backup:,mutable:,locked_round_trip_verified:,
      timeout:600,wait_timeout:120,device_root_method:'vendor_su',recovery_root_method:'adbd')
      raise A133Unlock::Invalid,'locked_round_trip_required' unless locked_round_trip_verified==true
      raise A133Unlock::Invalid,'invalid_unlock_options' unless adb.is_a?(String) && !adb.empty? &&
        adb.encoding.ascii_compatible? && adb.valid_encoding? && !adb.b.include?("\0") &&
        wait_timeout.is_a?(Integer) && wait_timeout.between?(1,7200) &&
        [device_root_method,recovery_root_method].all? { |value| %w[adbd vendor_su].include?(value) }
      @directory=directory.is_a?(String) ? directory.dup.freeze : directory
      @adb,@serial,@cid=adb.dup.freeze,serial.is_a?(String) ? serial.dup.freeze : serial,cid.is_a?(String) ? cid.dup.freeze : cid
      @timeout,@wait_timeout=timeout,wait_timeout
      @roots={'device'=>device_root_method.dup.freeze,'recovery'=>recovery_root_method.dup.freeze}.freeze
      @channel=A133Usb::Channel.new(@adb,@timeout)
      raise A133Unlock::Invalid,'original_environment_size_mismatch' unless original_env.is_a?(String)
      original=original_env.b.dup.freeze
      @base=A133Recovery::Policy.new(serial:@serial,cid:@cid,original_env:original,backup:backup).data
      @original_prefix=@base[:original_prefix]
      @armed_prefix=A133Unlock.prepare(@original_prefix)
      @armed_sha=Digest::SHA256.hexdigest(@armed_prefix+original.byteslice(131072,16777216-131072)).freeze
      values=A133Env.decode(@original_prefix).fetch(:entries).to_h.merge(A133Unlock::UPDATES.reject { |key,_| key=='hook' })
      @consumed=values.to_h { |key,value| [key.dup.freeze,value.dup.freeze] }.freeze
      parts=mutable.is_a?(Hash) ? mutable['partition_sha256'] : nil
      raise A133Unlock::Invalid,'invalid_unlock_mutable' unless mutable.is_a?(Hash) &&
        mutable['serial']==@serial && mutable['cid']==@cid && mutable['status']=='mutable_integrity_verified' &&
        mutable['mutable_copies_verified']==true && mutable['gpt_sha256']==@base[:gpt_sha] &&
        mutable['observation']=='recovery_unmounted_two_matching_reads' &&
        mutable['filesystem_consistency']=='not_established_by_integrity_check' && Journal.exact?(parts,%w[UDISK metadata]) &&
        parts.values.all? { |value| value.is_a?(String) && value.encoding.ascii_compatible? && value.ascii_only? && value.match?(/\A[0-9a-f]{64}\z/) }
      pinned=parts.to_h { |key,value| [key,value.dup.freeze] }.freeze
      @context={'serial'=>@serial,'cid'=>@cid,'backup_sha256'=>backup['uncompressed_sha256'].dup.freeze,
        'gpt_sha256'=>@base[:gpt_sha],'bootloader_sha256'=>@base[:bootloader_sha],
        'boot_sha256'=>@base[:boot_sha],'recovery_sha256'=>@base[:recovery_sha],
        'original_env_sha256'=>@base[:original_sha],'unlock_env_sha256'=>@armed_sha,
        'mutable_sha256'=>Journal.digest(pinned)}.freeze
    end
    def inventory(timeout=@timeout)
      out=A133Usb::Channel.new(@adb,timeout).run(['devices','-l'])[:output]
      rows=out.lines.map(&:split).reject { |row| row.empty? || row.first=='List' || row.first.start_with?('*') }.select { |row| row.first==@serial }
      raise A133Unlock::Invalid,'usb_unlock_duplicate_device' if rows.size>1
      return nil if rows.empty?
      row=rows.first
      raise A133Unlock::Invalid,'usb_unlock_transport_required' unless row.any? { |value| value.start_with?('usb:') }
      row[1]
    end
    def hash_role(role)
      @source.environment_inspect!
      sha=Digest::SHA256.new
      @source.read(role) { |chunk| sha.update(chunk) }
      @source.environment_inspect!
      sha.hexdigest
    end
    def observe
      state=inventory
      raise A133Unlock::Invalid,'usb_unlock_device_not_ready' unless @roots.key?(state)
      @source=A133UsbBackup::Source.new(adb:@adb,serial:@serial,state:state,root_method:@roots.fetch(state),timeout:@timeout)
      profile=@source.environment_inspect!
      raise A133Unlock::Invalid,'usb_unlock_identity_mismatch' unless profile[:serial]==@serial && profile[:cid]==@cid
      raise A133Unlock::Invalid,'usb_unlock_gpt_mismatch' unless profile[:gpt_sha256]==@base[:gpt_sha]
      %w[bootloader boot recovery].each do |role|
        raise A133Unlock::Invalid,'usb_'+role+'_changed' unless hash_role(role)==@base.fetch((role+'_sha').to_sym)
      end
      sha,tail=Digest::SHA256.new,Digest::SHA256.new
      prefix=''.b; offset=0
      @source.environment_inspect!
      @source.read('env') do |chunk|
        sha.update(chunk)
        prefix<<chunk.byteslice(0,[131072-prefix.bytesize,chunk.bytesize].min) if prefix.bytesize<131072
        skip=[131072-offset,0].max
        tail.update(chunk.byteslice(skip,chunk.bytesize-skip)) if skip<chunk.bytesize
        offset+=chunk.bytesize
      end
      @source.environment_inspect!
      mode=if sha.hexdigest==@base[:original_sha] then 'original'
        elsif sha.hexdigest==@armed_sha then 'armed'
        elsif tail.hexdigest==@base[:tail_sha] && A133Env.decode(prefix).fetch(:entries).to_h==@consumed then 'consumed'
        end
      raise A133Unlock::Invalid,'usb_unlock_environment_unknown' unless mode
      hardware=@store.snapshot['hardware_bytes']
      raise A133Unlock::Invalid,'unlock_hardware_changed' if hardware && hardware!=profile[:hardware_bytes]
      profile.slice(:state,:locked,:verified,:hardware_bytes).merge(mode:mode)
    rescue A133Env::Invalid
      raise A133Unlock::Invalid,'usb_unlock_environment_unknown',cause:nil
    end
    def record(phase,**values)
      @store.record(phase,**values)
      @latest=@store.report
    end
    def write_prefix(phase,prefix,before,expected)
      record(phase,write:true)
      raise A133Unlock::Invalid,'usb_unlock_source_changed' unless observe==before
      command='dd of=/dev/block/mmcblk0p2 bs=1048576 conv=notrunc 2>/dev/null && sync'
      if @roots.fetch(before[:state])=='vendor_su'
        command="exec /system/xbin/su 0 /system/bin/sh -c #{Shellwords.escape(command)}"
      end
      result=@channel.run(['-s',@serial,'shell','-T',command],source:StringIO.new(prefix),input_bytes:131072)
      raise A133Unlock::Invalid,'usb_unlock_input_changed' unless result[:input_sha256]==Digest::SHA256.hexdigest(prefix)
      after=observe
      raise A133Unlock::Invalid,'usb_unlock_readback_mismatch' unless after==before.merge(mode:expected)
      after
    end
    def wait_recovery
      deadline=Process.clock_gettime(Process::CLOCK_MONOTONIC)+@wait_timeout
      loop do
        remaining=deadline-Process.clock_gettime(Process::CLOCK_MONOTONIC)
        raise A133Unlock::Invalid,'usb_unlock_wait_timeout' if remaining<=0
        state=inventory([@timeout,remaining.ceil].min)
        raise A133Unlock::Invalid,'usb_unlock_wait_timeout' if Process.clock_gettime(Process::CLOCK_MONOTONIC)>=deadline
        return if state=='recovery'
        raise A133Unlock::Invalid,'usb_unlock_device_not_ready' unless state.nil? || %w[device offline].include?(state)
        remaining=deadline-Process.clock_gettime(Process::CLOCK_MONOTONIC)
        sleep [remaining,0.1].min if remaining>0
      end
    end
    def perform
      data=@store.snapshot
      wait_recovery if data['possible_reboot']
      current=observe
      if !data['possible_write']
        raise A133Unlock::Invalid,'usb_unlock_initial_state' unless current.values_at(:state,:locked,:verified,:mode)==%w[device 1 green original]
      end
      record('checking',hardware_bytes:current[:hardware_bytes]) unless data['hardware_bytes']
      if current[:state]=='device'
        raise A133Unlock::Invalid,'usb_unlock_resume_state' unless current[:locked]=='1' && current[:verified]=='green' && %w[original armed].include?(current[:mode])
        current=write_prefix('arming',@armed_prefix,current,'armed') if current[:mode]=='original'
        record('armed')
        raise A133Unlock::Invalid,'usb_unlock_reboot_already_recorded' if @store.snapshot['possible_reboot']
        record('rebooting',reboot:true)
        @channel.run(['-s',@serial,'reboot'])
        wait_recovery
        current=observe
      end
      raise A133Unlock::Invalid,'usb_unlock_return_state' unless current[:state]=='recovery' && current[:locked]=='0' &&
        current[:verified]=='orange' && %w[consumed original].include?(current[:mode])
      current=write_prefix('restoring',@original_prefix,current,'original') if current[:mode]=='consumed'
      record('verified')
      @store.report.merge(status:'unlocked_recovery_verified',env_sha256:@base[:original_sha])
    end
    def execute
      started=true
      Journal.open(@directory,@context) do |store|
        @store=store
        begin
          @latest=store.report
          perform
        rescue StandardError => error
          reason=self.class.reason(error)
          begin
            record('failed',reason:reason)
          rescue StandardError
            # Preserve the original failure and conservative prior intent.
          end
          report=@latest || store.report
          if reason.start_with?('unlock_journal_')
            report=report.merge(effects_unknown:true,possible_write:true,possible_reboot:true)
          end
          raise Invalid.new(reason,report.merge(status:'unlock_stage_stopped')),cause:nil
        end
      end
    rescue Invalid
      raise
    rescue StandardError => error
      report=(@latest || {possible_write:started,possible_reboot:started,installation_ready:false}).merge(status:'unlock_stage_stopped',effects_unknown:true)
      raise Invalid.new(self.class.reason(error),report),cause:nil
    end
    private :initialize,:inventory,:hash_role,:observe,:record,:write_prefix,:wait_recovery,:perform
  end
end

if $PROGRAM_NAME==__FILE__
  begin
    raise A133Unlock::Invalid,'invalid_arguments' unless ARGV.size==1
    report=nil
    A133Unlock::Journal.open(ARGV.first,nil) { |store| report=store.report }
    puts JSON.pretty_generate(report)
  rescue A133Unlock::Invalid,SystemCallError,IOError => error
    puts JSON.pretty_generate(status:'unlock_inspection_stopped',reason:A133Unlock::Stage.reason(error),installation_ready:false)
    exit 1
  end
end
