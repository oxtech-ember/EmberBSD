#!/usr/bin/env ruby
# Origin: EmberBSD - bounded single-reboot factory recovery and Android return transitions.
require_relative 'a133-recovery-readback'
module A133Recovery
  class Reboot
    class Invalid < A133Usb::Invalid
      attr_reader :reboot_attempted
      def initialize(reason,attempted)
        super(reason)
        @reboot_attempted=attempted
        self.write_attempted=attempted
      end
    end
    def initialize(adb:,serial:,cid:,device_root_method:'vendor_su',recovery_root_method:'adbd',timeout:600,wait_timeout:120)
      Policy.identity!(serial,cid)
      raise A133Usb::Invalid,'invalid_reboot_options' unless adb.is_a?(String) && !adb.empty? &&
        adb.encoding.ascii_compatible? && adb.valid_encoding? && !adb.b.include?("\0") &&
        wait_timeout.is_a?(Integer) && (1..7200).cover?(wait_timeout)
      @serial,@cid,@adb=serial.dup.freeze,cid.dup.freeze,adb.dup.freeze
      @device_root=device_root_method.is_a?(String) ? device_root_method.dup.freeze : device_root_method
      @recovery_root=recovery_root_method.is_a?(String) ? recovery_root_method.dup.freeze : recovery_root_method
      @timeout,@wait_timeout=timeout,wait_timeout
      @channel=A133Usb::Channel.new(@adb,@timeout)
      # Validate both source configurations without launching a subprocess.
      [['device',@device_root],['recovery',@recovery_root]].each do |state,root|
        A133UsbBackup::Source.new(adb:@adb,serial:@serial,state:state,root_method:root,timeout:@timeout)
      end
    rescue A133Usb::Invalid,A133UsbBackup::Invalid => error
      raise_failure(error,false)
    end
    def raise_failure(error,attempted)
      reason=error.is_a?(SystemCallError) || error.is_a?(IOError) ? 'usb_reboot_io_failed' : error.message
      raise Invalid.new(reason,attempted),cause:nil
    end
    def monotonic
      Process.clock_gettime(Process::CLOCK_MONOTONIC)
    end
    def wait_for(source,destination)
      deadline=monotonic+@wait_timeout
      loop do
        remaining=deadline-monotonic
        raise A133Usb::Invalid,'usb_reboot_wait_timeout' if remaining<=0
        output=A133Usb::Channel.new(@adb,[@timeout,remaining.ceil].min).run(['devices','-l'])[:output]
        rows=output.lines.map(&:split).reject { |row| row.empty? || row.first=='List' || row.first.start_with?('*') }
        rows=rows.select { |row| row.first==@serial }
        raise A133Usb::Invalid,'usb_reboot_duplicate_device' if rows.size>1
        unless rows.empty?
          row=rows.first
          raise A133Usb::Invalid,'usb_reboot_transport_required' unless row.any? { |field| field.start_with?('usb:') }
          return if row[1]==destination
          raise A133Usb::Invalid,'usb_reboot_device_not_ready' unless row[1]=='offline' || row[1]==source
        end
        remaining=deadline-monotonic
        sleep [0.1,remaining].min if remaining>0
      end
    end
    def readback(policy,state,expected)
      root=state=='device' ? @device_root : @recovery_root
      Readback.new(adb:@adb,policy:policy,state:state,root_method:root,timeout:@timeout).verify(expected:expected)
    end
    def transition(action,original_env,backup)
      attempted=false
      policy=Policy.new(serial:@serial,cid:@cid,original_env:original_env,backup:backup)
      source,destination=action==:enter ? %w[device recovery] : %w[recovery device]
      before=readback(policy,source,action==:enter ? 'armed' : 'original')
      attempted=true
      @channel.run(['-s',@serial,'reboot'])
      wait_for(source,destination)
      after=readback(policy,destination,action==:enter ? 'consumed' : 'original')
      raise A133Usb::Invalid,'usb_reboot_hardware_changed' unless before[:hardware_bytes]==after[:hardware_bytes]
      {status:action==:enter ? 'factory_recovery_verified' : 'factory_android_verified',
        direct_writes_performed:0,reboots_submitted:1,storage_side_effects_possible:true,
        installation_ready:false,destination:after}
    rescue A133Usb::Invalid,A133UsbBackup::Invalid,A133Recovery::Invalid,SystemCallError,IOError => error
      raise_failure(error,attempted)
    end
    def enter_recovery(original_env:,backup:)
      transition(:enter,original_env,backup)
    end
    def return_android(original_env:,backup:)
      transition(:return,original_env,backup)
    end
    private :raise_failure,:monotonic,:wait_for,:readback,:transition
  end
end
