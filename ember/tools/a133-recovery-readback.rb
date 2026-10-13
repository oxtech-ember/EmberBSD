#!/usr/bin/env ruby
# Origin: EmberBSD - zero-write factory state verification around complete critical reads.
require_relative 'a133-recovery-policy'
require_relative 'a133-usb-backup-source'
module A133Recovery
  class Readback
    def initialize(adb:,policy:,state:,root_method:,timeout:600)
      raise A133Usb::Invalid,'invalid_readback_policy' unless policy.is_a?(Policy)
      raise A133Usb::Invalid,'invalid_usb_options' unless adb.is_a?(String) && !adb.empty? &&
        adb.encoding.ascii_compatible? && adb.valid_encoding? && !adb.b.include?("\0")
      @policy=policy
      @state=state.is_a?(String) ? state.dup.freeze : state
      @source=A133UsbBackup::Source.new(adb:adb.is_a?(String) ? adb.dup.freeze : adb,
        serial:policy.data[:serial],state:@state,root_method:root_method,timeout:timeout)
    rescue A133Usb::Invalid,A133UsbBackup::Invalid => error
      raise_failure(error)
    end
    def raise_failure(error)
      reason=error.is_a?(SystemCallError) || error.is_a?(IOError) ? 'usb_readback_io_failed' : error.message
      failure=A133Usb::Invalid.new(reason); failure.write_attempted=false
      raise failure,cause:nil
    end
    def read_hash(role)
      @source.environment_inspect!
      digest=Digest::SHA256.new
      @source.read(role) { |chunk| digest.update(chunk) }
      @source.environment_inspect!
      digest.hexdigest
    end
    def guard!
      profile=@source.environment_inspect!
      data=@policy.data
      raise A133Usb::Invalid,'usb_readback_identity_mismatch' unless profile[:serial]==data[:serial] && profile[:cid]==data[:cid]
      raise A133Usb::Invalid,'usb_readback_state_unsupported' unless profile[:locked]=='1' && profile[:verified]=='green'
      raise A133Usb::Invalid,'usb_readback_gpt_mismatch' unless profile[:gpt_sha256]==data[:gpt_sha]
      %w[bootloader boot recovery].each do |role|
        raise A133Usb::Invalid,'usb_'+role+'_changed' unless read_hash(role)==data.fetch((role+'_sha').to_sym)
      end
      profile
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
    def verify(expected:)
      expected=expected.dup.freeze if expected.is_a?(String)
      raise A133Usb::Invalid,'invalid_environment_expectation' unless
        expected=='original' || (expected=='armed' && @state=='device') || (expected=='consumed' && @state=='recovery')
      guard!
      current=read_environment
      raise A133Usb::Invalid,'usb_environment_unexpected' unless @policy.matches?(expected,current)
      profile=guard!
      {status:'environment_state_verified',state:@state,expected:expected,env_sha256:current[:sha],
        hardware_bytes:profile[:hardware_bytes],writes_performed:0,installation_ready:false}
    rescue A133Usb::Invalid,A133UsbBackup::Invalid,A133Recovery::Invalid,SystemCallError,IOError => error
      raise_failure(error)
    end
    private :raise_failure,:read_hash,:guard!,:read_environment
  end
end
