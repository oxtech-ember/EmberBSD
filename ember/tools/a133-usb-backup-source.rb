#!/usr/bin/env ruby
# Origin: EmberBSD - pinned root-readable A133 USB source; only inspection and dd reads.
require 'shellwords'
require_relative 'a133-usb-channel'
require_relative 'a133-backup-check'

module A133UsbBackup
  class Invalid < StandardError; end
  class Source
    def initialize(adb:,serial:,state:,root_method:,timeout:)
      raise Invalid,'invalid_arguments' unless (serial.nil? || valid_serial?(serial)) &&
        %w[device recovery].include?(state) && %w[vendor_su adbd].include?(root_method)
      @serial = serial && serial.dup.freeze
      @state,@root_method = state.dup.freeze,root_method.dup.freeze
      @channel = A133Usb::Channel.new(adb.is_a?(String) ? adb.dup.freeze : adb,timeout)
    rescue A133Usb::Invalid => error
      raise Invalid,error.message,cause:nil
    end

    def valid_serial?(value)
      value.is_a?(String) && value.bytesize.between?(1,128) && value.match?(/\A[a-zA-Z0-9._-]+\z/)
    end

    def command(value)
      @root_method=='adbd' ? value : "exec /system/xbin/su 0 /system/bin/sh -c #{Shellwords.escape(value)}"
    end

    def query(value,limit:16384)
      @channel.run(['-s',@serial,'shell','-T',command(value)],limit:limit)[:output].strip
    end

    def range(skip,count)
      @channel.run(['-s',@serial,'shell','-T',command("exec dd if=/dev/block/mmcblk0 bs=512 skip=#{skip} count=#{count} 2>/dev/null")],limit:count*512)[:output]
    end

    def recovery_unmounted!(inventory_command='cat /proc/self/mountinfo')
      rows = query(inventory_command,limit:1048576).lines.map(&:split)
      raise Invalid,'usb_mount_inventory_invalid' if rows.empty? || !rows.all? do |fields|
        fields.size>=10 && fields[2].match?(/\A[0-9]+:[0-9]+\z/) && (fields.index('-') || 0)>=6
      end
      ids = rows.map { |fields| fields[2] }.uniq
      mounted = query("for d in #{ids.join(' ')}; do if [ -e /sys/dev/block/$d ]; then echo $d; fi; done")
      raise Invalid,'usb_target_mounted' unless mounted.empty?
    end

    def inspect!
      rows = @channel.run(['devices','-l'])[:output].lines.map(&:split).reject do |row|
        row.empty? || row.first=='List' || row.first.start_with?('*')
      end
      rows = rows.select { |row| row.first==@serial } if @serial
      raise Invalid,'usb_backup_expected_one_device' unless rows.size==1
      row = rows.first
      raise Invalid,'usb_backup_device_not_ready' unless valid_serial?(row.first) && row[1]==@state &&
        row.any? { |field| field.start_with?('usb:') }
      @serial ||= row.first.dup.freeze
      features = @channel.run(['-s',@serial,'features'])[:output].strip.split(',')
      raise Invalid,'usb_shell_v2_required' unless features.include?('shell_v2')
      raise Invalid,'usb_root_required' unless query('id -u')=='0'
      raise Invalid,'usb_board_mismatch' unless query('getprop ro.product.model')=='a133' &&
        query('cat /proc/device-tree/compatible').split("\0").include?('allwinner,a133')
      inventory = A133Backup::INVENTORY
      paths = ['/sys/class/block/mmcblk0/size','/sys/class/block/mmcblk0/device/cid'] +
        inventory.flat_map { |part| ["/sys/class/block/mmcblk0p#{part[:index]}/start","/sys/class/block/mmcblk0p#{part[:index]}/size"] } +
        ['/sys/class/block/mmcblk0boot0/size','/sys/class/block/mmcblk0boot1/size']
      values = query('cat '+paths.join(' ')).lines.map(&:strip)
      cid = values[1]
      raise Invalid,'usb_backup_invalid_cid' unless cid.is_a?(String) && cid.match?(/\A[0-9a-f]{32}\z/)
      numbers = [values.first]+values.drop(2)
      expected = [A133Backup::BYTES/512]+inventory.flat_map { |part| [part[:start],part[:sectors]] }
      raise Invalid,'usb_partition_layout_mismatch' unless numbers.size==expected.size+2 &&
        numbers.all? { |value| value && value.match?(/\A[0-9]+\z/) } && numbers.first(expected.size).map(&:to_i)==expected
      hardware = numbers.last(2).map(&:to_i)
      raise Invalid,'usb_backup_hardware_layout_mismatch' unless hardware[0]==hardware[1] &&
        hardware[0]>0 && hardware[0]<=65536
      mapping = "for p in #{inventory.map { |part| part[:name] }.join(' ')}; do readlink -f /dev/block/by-name/$p || exit 1; done"
      raise Invalid,'usb_partition_mapping_mismatch' unless query(mapping).lines.map(&:strip)==
        inventory.map { |part| "/dev/block/mmcblk0p#{part[:index]}" }
      recovery_unmounted! if @state=='recovery'
      total = A133Backup::BYTES/512
      head,tail = range(0,34),range(total-34,34)
      A133Backup.validate_gpt(head,tail,A133Backup::BYTES,inventory)
      profile = {serial:@serial,cid:cid,state:@state,root_method:@root_method,hardware_bytes:hardware[0]*512,
        gpt_sha256:Digest::SHA256.hexdigest(head+tail),locked:query('getprop ro.boot.flash.locked'),
        verified:query('getprop ro.boot.verifiedbootstate')}
      raise Invalid,'usb_backup_source_changed' if @profile && @profile!=profile
      @profile ||= profile
      @profile.dup
    rescue Invalid,A133Usb::Invalid,A133Backup::Invalid => error
      raise Invalid,error.message,cause:nil
    end

    def mutable_inspect!
      raise Invalid,'usb_mutable_requires_recovery' unless @state=='recovery'
      profile=inspect!
      recovery_unmounted!('for p in /proc/[0-9]*; do for t in "$p"/task/[0-9]*; do cat "$t/mountinfo" || exit 1; done; done')
      swaps=query('cat /proc/swaps').lines.map(&:split)
      raise Invalid,'usb_swap_inventory_invalid' unless swaps.first==%w[Filename Type Size Used Priority]
      paths=['/sys/class/block/mmcblk0']+A133Backup::INVENTORY.map { |part| "/sys/class/block/mmcblk0p#{part[:index]}" }
      holders=query('for p in '+paths.join(' ')+'; do [ -d "$p/holders" ] && [ -r "$p/holders" ] && [ -x "$p/holders" ] || exit 1; for d in "$p"/holders/*; do if [ -e "$d" ]; then echo "$d"; fi; done; done')
      raise Invalid,'usb_target_in_use' unless swaps.size==1 && holders.empty?
      profile
    rescue Invalid,A133Usb::Invalid,A133Backup::Invalid => error
      raise Invalid,error.message,cause:nil
    end

    def environment_inspect!
      return mutable_inspect! if @state=='recovery'
      profile=inspect!
      ids=query('cat /sys/class/block/mmcblk0/dev /sys/class/block/mmcblk0p2/dev').lines.map(&:strip)
      raise Invalid,'usb_environment_device_ids_invalid' unless ids.size==2 && ids.uniq.size==2 &&
        ids.all? { |id| id.match?(/\A[0-9]+:[0-9]+\z/) }
      rows=query('for p in /proc/[0-9]*; do for t in "$p"/task/[0-9]*; do cat "$t/mountinfo" || exit 1; done; done',limit:1048576).lines.map(&:split)
      raise Invalid,'usb_mount_inventory_invalid' if rows.empty? || !rows.all? do |fields|
        fields.size>=10 && fields[2].match?(/\A[0-9]+:[0-9]+\z/) && (fields.index('-') || 0)>=6
      end
      raise Invalid,'usb_target_mounted' if rows.any? { |fields| ids.include?(fields[2]) }
      swaps=query('cat /proc/swaps').lines.map(&:split)
      raise Invalid,'usb_swap_inventory_invalid' unless swaps.first==%w[Filename Type Size Used Priority]
      holders=query('for p in /sys/class/block/mmcblk0 /sys/class/block/mmcblk0p2; do [ -d "$p/holders" ] && [ -r "$p/holders" ] && [ -x "$p/holders" ] || exit 1; for d in "$p"/holders/*; do if [ -e "$d" ]; then echo "$d"; fi; done; done')
      raise Invalid,'usb_target_in_use' unless swaps.size==1 && holders.empty?
      profile
    rescue Invalid,A133Usb::Invalid,A133Backup::Invalid => error
      raise Invalid,error.message,cause:nil
    end

    def read(role)
      mutable=%w[UDISK metadata].include?(role)
      profile = mutable ? mutable_inspect! : inspect!
      if role=='disk'
        target,bytes = '/dev/block/mmcblk0',A133Backup::BYTES
      elsif %w[boot0 boot1].include?(role)
        target,bytes = '/dev/block/mmcblk0'+role,profile.fetch(:hardware_bytes)
      elsif %w[bootloader env boot recovery UDISK metadata].include?(role)
        part = A133Backup::INVENTORY.find { |entry| entry[:name]==role }
        target,bytes = "/dev/block/mmcblk0p#{part.fetch(:index)}",part.fetch(:sectors)*512
      else
        raise Invalid,'invalid_backup_role'
      end
      raise Invalid,'invalid_read_callback' unless block_given?
      count = 0
      value = "exec dd if=#{target} bs=1048576 count=#{(bytes+1048575)/1048576} 2>/dev/null"
      @channel.run(['-s',@serial,'shell','-T',command(value)],limit:bytes) do |chunk|
        raise Invalid,'usb_backup_stream_size_mismatch' if count+chunk.bytesize>bytes
        yield chunk
        count += chunk.bytesize
      end
      raise Invalid,'usb_backup_stream_size_mismatch' unless count==bytes
      mutable ? mutable_inspect! : inspect!
      count
    rescue Invalid,A133Usb::Invalid,A133Backup::Invalid => error
      raise Invalid,error.message,cause:nil
    end
    private :valid_serial?,:command,:query,:range,:recovery_unmounted!
  end
end
