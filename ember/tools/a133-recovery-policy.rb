#!/usr/bin/env ruby
# Origin: EmberBSD - immutable captured factory environment policy shared by transitions.
require_relative 'a133-recovery-env'
require_relative 'a133-backup-check'
require_relative 'a133-usb-channel'
require_relative 'a133-usb-backup-source'
module A133Recovery
  class DirectSource < A133UsbBackup::Source
    private
    def supported_model?(model)
      model=='QUAD-CORE A133 c3'
    end
  end
  class Policy
    attr_reader :data
    def self.identity!(serial,cid)
      raise A133Usb::Invalid,'invalid_usb_identity' unless serial.is_a?(String) && serial.ascii_only? &&
        serial.bytesize.between?(1,128) && serial.match?(/\A[a-zA-Z0-9._-]+\z/) &&
        cid.is_a?(String) && cid.ascii_only? && cid.match?(/\A[0-9a-f]{32}\z/)
    end
    def sha?(value)
      value.is_a?(String) && value.ascii_only? && value.match?(/\A[0-9a-f]{64}\z/)
    end
    def initialize(serial:,cid:,original_env:,backup:)
      self.class.identity!(serial,cid)
      @serial,@cid=serial.dup.freeze,cid.dup.freeze

      hashes=backup.is_a?(Hash) ? backup['partition_sha256'] : nil
      raise A133Usb::Invalid,'invalid_entry_backup' unless backup.is_a?(Hash) &&
        backup['serial']==@serial && backup['cid']==@cid && backup['status']=='backup_integrity_verified' &&
        backup['bytes']==A133Backup::BYTES && %w[gpt_headers_crc gpt_arrays_crc partition_layout_verified
          critical_copies_verified hardware_boot_copies_verified].all? { |key| backup[key]==true } &&
        sha?(backup['uncompressed_sha256']) && sha?(backup['gpt_sha256']) && hashes.is_a?(Hash) &&
        hashes.keys.all? { |key| key.is_a?(String) } &&
        hashes.keys.sort==%w[boot bootloader env recovery] && hashes.values.all? { |value| sha?(value) }
      pinned=%w[env bootloader boot recovery].each_with_object({}) { |key,out| out[key]=hashes[key].dup.freeze }
      gpt_sha=backup['gpt_sha256'].dup.freeze
      bytes=A133Backup::INVENTORY.find { |part| part[:name]=='env' }.fetch(:sectors)*512
      raise A133Usb::Invalid,'original_environment_size_mismatch' unless original_env.is_a?(String) && original_env.bytesize==bytes
      original=original_env.b.dup.freeze
      raise A133Usb::Invalid,'original_environment_hash_mismatch' unless Digest::SHA256.hexdigest(original)==pinned['env']
      prefix=original.byteslice(0,A133Env::SIZE).freeze
      plan=A133Recovery.entry_plan(prefix)
      @profile=plan.fetch(:profile)
      armed=plan.fetch(:armed_env)
      tail=original.byteslice(A133Env::SIZE,bytes-A133Env::SIZE)
      consumed=plan.fetch(:consumed)
      values={serial:@serial,cid:@cid,original_sha:pinned['env'],armed_sha:Digest::SHA256.hexdigest(armed+tail),
        original_prefix:prefix,armed_prefix:armed,tail_sha:Digest::SHA256.hexdigest(tail),
        consumed:consumed,boot_sha:pinned['boot'],bootloader_sha:pinned['bootloader'],recovery_sha:pinned['recovery'],gpt_sha:gpt_sha}.freeze
      @data=values.transform_values { |value| value.is_a?(String) ? value.freeze : value }.freeze
      freeze
    end
    def source(adb:,state:,root_method:,timeout:)
      type=@profile=='direct' ? DirectSource : A133UsbBackup::Source
      type.new(adb:adb,serial:@serial,state:state,root_method:root_method,timeout:timeout)
    end
    def consumed_values_match?(actual,expected=@data[:consumed])
      return true if actual==expected
      return false unless @profile=='direct' && %w[mac wifi_mac bt_mac specialstr].all? { |k| expected[k]=='' }
      runtime={'bootreason'=>'unknow','fdtcontroladdr'=>'7beb3280','fileaddr'=>'40000000',
        'filesize'=>'2a338','rotpk_status'=>'0',
        'partitions'=>A133Backup::INVENTORY.map { |part| "#{part[:name]}@mmcblk0p#{part[:index]}" }.join(':')}
      return false unless !expected.key?('snum') && actual['snum'].is_a?(String) && actual['snum'].match?(/\A[0-9a-f]{19}\z/)
      return false unless runtime.keys.none? { |k| expected.key?(k) } && runtime.all? { |k,v| actual[k]==v }
      mac=actual['mac']
      return false unless mac.is_a?(String) && mac.match?(/\A(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\z/)
      octets=mac.split(':').map { |v| v.to_i(16) }
      return false unless octets.first&1==0 && octets.any? { |v| v!=0 }
      return false unless %w[wifi_mac bt_mac specialstr].none? { |k| actual.key?(k) }
      normalized=actual.reject { |k,_| runtime.key?(k) || k=='snum' }
      %w[mac wifi_mac bt_mac specialstr].each { |k| normalized[k]='' }
      normalized==expected
    end

    def consumed_prefix_matches?(prefix,expected:)
      consumed_values_match?(A133Env.decode(prefix).fetch(:entries).to_h,expected)
    rescue A133Env::Invalid
      false
    end

    def matches?(expected,current)
      case expected
      when 'original' then current[:sha]==@data[:original_sha]
      when 'armed' then current[:sha]==@data[:armed_sha]
      when 'consumed'
        current[:tail_sha]==@data[:tail_sha] && consumed_values_match?(A133Env.decode(current[:prefix]).fetch(:entries).to_h)
      else false
      end
    rescue A133Env::Invalid
      false
    end
    private :sha?,:consumed_values_match?
  end
end
