# Origin: EmberBSD - one guarded MCU action in vendor Linux recovery.
require 'shellwords'
require 'stringio'
require_relative 'a133-recovery-policy'

module A133Recovery
  class McuControl
    class Invalid < A133Usb::Invalid
      attr_reader :action_attempted
      def initialize(reason, attempted=false)
        super(reason)
        @action_attempted=attempted
      end
    end

    def initialize(adb:, serial:, cid:, model:, timeout:10)
      adb,serial,cid,model=[adb,serial,cid,model].map { |value| pinned_string(value) }
      Policy.identity!(serial,cid)
      raise Invalid,'invalid_mcu_model' unless ['a133','QUAD-CORE A133 c3'].include?(model)
      @serial,@cid,@model=serial,cid,model
      @channel=A133Usb::Channel.new(adb,timeout)
    end

    def run(action:, helper:, sha256:, boot_id:)
      attempted=false
      action,sha256,boot_id=[action,sha256,boot_id].map { |value| pinned_string(value) }
      raise Invalid,'invalid_mcu_action' unless %w[arm feed disable].include?(action)
      raise Invalid,'invalid_mcu_epoch' unless boot_id.is_a?(String) &&
        boot_id.match?(/\A[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\z/)
      raise Invalid,'invalid_mcu_digest' unless sha256.is_a?(String) && sha256.match?(/\A[0-9a-f]{64}\z/)

      command=remote_command(action,sha256,boot_id)
      bytes=File.open(helper,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |file|
        raise Invalid,'invalid_mcu_helper' unless file.stat.file? && file.stat.size.between?(1,65536)
        file.read(65537)
      end
      raise Invalid,'mcu_helper_changed' unless bytes && bytes.bytesize.between?(1,65536) &&
        Digest::SHA256.hexdigest(bytes)==sha256
      rows=@channel.run(%w[devices -l])[:output].lines.map(&:split).select { |row| row.first==@serial }
      raise Invalid,'recovery_usb_required' unless rows.size==1 && rows.first[1]=='recovery' &&
        rows.first.any? { |field| field.start_with?('usb:') && field.size>4 }
      features=@channel.run(['-s',@serial,'features'])[:output].split(/[,\s]+/)
      raise Invalid,'usb_shell_v2_required' unless features.include?('shell_v2')

      # From dispatch onward the MCU may already have accepted the ioctl.
      attempted=true
      raw=@channel.run(['-s',@serial,'shell','-T',command],
        source:StringIO.new(bytes),input_bytes:bytes.bytesize,limit:56).fetch(:output)
      raise Invalid.new('mcu_frame_size',true) unless raw.bytesize==56
      version,before,first,enable,period,after,magic=raw.unpack('Q<7')
      raise Invalid.new('mcu_ack_invalid',true) unless version==0x170307 && magic==0xc360 &&
        first==7 && before.between?(0,255) && after.between?(0,255) &&
        [enable,period]==(action=='arm' ? [7,7] : [0,0])
      true
    rescue Invalid
      raise
    rescue A133Usb::Invalid => e
      raise Invalid.new(e.message,attempted), cause:nil
    rescue IOError, SystemCallError
      raise Invalid.new('mcu_io_failed',attempted), cause:nil
    end

    private
    def pinned_string(value)
      String.new(value).freeze if value.is_a?(String)
    end

    def remote_command(action,sha256,boot_id)
      states=action=='arm' ? '0:orange' : '0:orange|1:green'
      <<~SH
        set -eu
        umask 077
        locked=$(getprop ro.boot.flash.locked)
        verified=$(getprop ro.boot.verifiedbootstate)
        case "$locked:$verified" in #{states}) ;; *) exit 1 ;; esac
        identity() {
          [ "$(id -u)" = 0 ] &&
          [ "$(getprop ro.product.model)" = #{Shellwords.escape(@model)} ] &&
          [ "$(cat /sys/class/block/mmcblk0/device/cid)" = #{@cid} ] &&
          [ "$(cat /proc/sys/kernel/random/boot_id)" = #{boot_id} ] &&
          [ "$(getprop ro.boot.flash.locked)" = "$locked" ] &&
          [ "$(getprop ro.boot.verifiedbootstate)" = "$verified" ]
        }
        identity
        [ ! -L /tmp ]
        cd /tmp
        on_ram() {
          case "$(stat -f -c %t .)" in 1021994|858458f6) return 0 ;; *) return 1 ;; esac
        }
        on_ram
        case "$(stat -c %u:%a .)" in 0:*) ;; *) exit 1 ;; esac
        if [ ! -e ember-mcu7502 ]; then mkdir -m 700 ember-mcu7502; fi
        cd -P ./ember-mcu7502
        on_ram
        here=$(stat -c %d:%i:%u:%a .)
        case "$here" in *:0:700) ;; *) exit 1 ;; esac
        named=$(stat -c %d:%i:%u:%a /tmp/ember-mcu7502)
        [ "$here" = "$named" ]
        mkdir -m 700 #{action}
        trap 'rmdir #{action}' 0
        (
          cd ./#{action}
          trap 'rm -f helper' 0
          cat > helper
          chmod 700 helper
          hash=$(sha256sum helper)
          [ "${hash%% *}" = #{sha256} ]
          identity
          ./helper
          identity
        )
      SH
    end
  end
end
