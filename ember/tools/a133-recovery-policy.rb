#!/usr/bin/env ruby
# Origin: EmberBSD - immutable captured factory environment policy shared by transitions.
require_relative 'a133-recovery-env'
require_relative 'a133-backup-check'
require_relative 'a133-usb-channel'
module A133Recovery
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
      armed=A133Recovery.prepare(prefix).freeze
      tail=original.byteslice(A133Env::SIZE,bytes-A133Env::SIZE)
      consumed=A133Env.decode(prefix).fetch(:entries).to_h.merge(A133Recovery::UPDATES.reject { |key,_| key=='hook' })
      consumed=consumed.each_with_object({}) { |(key,value),out| out[key.dup.freeze]=value.dup.freeze }.freeze
      values={serial:@serial,cid:@cid,original_sha:pinned['env'],armed_sha:Digest::SHA256.hexdigest(armed+tail),
        original_prefix:prefix,armed_prefix:armed,tail_sha:Digest::SHA256.hexdigest(tail),
        consumed:consumed,boot_sha:pinned['boot'],bootloader_sha:pinned['bootloader'],recovery_sha:pinned['recovery'],gpt_sha:gpt_sha}.freeze
      @data=values.transform_values { |value| value.is_a?(String) ? value.freeze : value }.freeze
      freeze
    end
    def matches?(expected,current)
      case expected
      when 'original' then current[:sha]==@data[:original_sha]
      when 'armed' then current[:sha]==@data[:armed_sha]
      when 'consumed'
        current[:tail_sha]==@data[:tail_sha] && A133Env.decode(current[:prefix]).fetch(:entries).to_h==@data[:consumed]
      else false
      end
    rescue A133Env::Invalid
      false
    end
    private :sha?
  end
end
