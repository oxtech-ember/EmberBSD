#!/usr/bin/env ruby
# Origin: EmberBSD - guarded USB range-write API and read-only recovery inspection.
require 'json'
require 'optparse'
require_relative 'a133-usb-channel'
require_relative 'a133-backup-check'
require_relative 'a133-recovery-env'

module A133Usb
  class Client
    ROLES = {'boot'=>[3,33554432], 'resources'=>[1,33554432], 'root'=>[17,27676098048]}.freeze

    def initialize(adb:, serial:, cid:, timeout: 600, additional_guard:nil)
      raise Invalid,'invalid_additional_guard' unless additional_guard.nil? || additional_guard.respond_to?(:call)
      @additional_guard=additional_guard
      raise Invalid, 'invalid_usb_identity' unless serial.is_a?(String) &&
        serial.bytesize.between?(1,128) && serial.match?(/\A[a-zA-Z0-9._-]+\z/) &&
        cid.is_a?(String) && cid.match?(/\A[0-9a-f]{32}\z/)
      @serial, @cid = serial.dup.freeze, cid.dup.freeze
      @channel = Channel.new(adb,timeout)
    end

    def query(command)
      @channel.run(['-s',@serial,'shell','-T',command])[:output].strip
    end

    def range(index, skip, sectors, &block)
      target = index ? "/dev/block/mmcblk0p#{index}" : '/dev/block/mmcblk0'
      command = "exec dd if=#{target} bs=512 skip=#{skip} count=#{sectors} 2>/dev/null"
      @channel.run(['-s',@serial,'shell','-T',command], limit: sectors*512, &block)[:output]
    end

    def range_sha(index, bytes)
      digest = Digest::SHA256.new
      count = 0
      range(index,0,bytes/512) do |chunk|
        count += chunk.bytesize
        raise Invalid, 'usb_readback_size_mismatch' if count > bytes
        digest.update(chunk)
      end
      raise Invalid, 'usb_readback_size_mismatch' unless count == bytes
      digest.hexdigest
    end

    def inspect!
      devices = @channel.run(['devices','-l'])[:output].lines.map(&:split).select { |row| row.first == @serial }
      raise Invalid, 'usb_device_not_ready' unless devices.size == 1 && devices[0][1] == 'recovery' &&
        devices[0].any? { |field| field.start_with?('usb:') }
      features = @channel.run(['-s',@serial,'features'])[:output].strip.split(',')
      raise Invalid, 'usb_shell_v2_required' unless features.include?('shell_v2')
      raise Invalid, 'usb_root_required' unless query('id -u') == '0'
      raise Invalid, 'usb_recovery_unlock_required' unless query('getprop ro.boot.flash.locked') == '0' &&
        query('getprop ro.boot.verifiedbootstate') == 'orange'
      raise Invalid, 'usb_board_mismatch' unless query('getprop ro.product.model') == 'a133' &&
        query('cat /proc/device-tree/compatible').split("\0").include?('allwinner,a133')
      inventory = A133Backup::INVENTORY
      paths = ['/sys/class/block/mmcblk0/size','/sys/class/block/mmcblk0/device/cid'] +
        inventory.flat_map { |part| ["/sys/class/block/mmcblk0p#{part[:index]}/start",
          "/sys/class/block/mmcblk0p#{part[:index]}/size"] }
      values = query('cat ' + paths.join(' ')).lines.map(&:strip)
      raise Invalid, 'usb_identity_changed' unless values[1] == @cid
      numbers = [values.first] + values.drop(2)
      expected = [A133Backup::BYTES/512] + inventory.flat_map { |part| [part[:start],part[:sectors]] }
      raise Invalid, 'usb_partition_layout_mismatch' unless numbers.size == expected.size &&
        numbers.all? { |value| value && value.match?(/\A[0-9]+\z/) } && numbers.map(&:to_i) == expected
      mapping = "for p in #{inventory.map { |part| part[:name] }.join(' ')}; do readlink -f /dev/block/by-name/$p || exit 1; done"
      targets = query(mapping).lines.map(&:strip)
      raise Invalid, 'usb_partition_mapping_mismatch' unless targets ==
        inventory.map { |part| "/dev/block/mmcblk0p#{part[:index]}" }
      critical_ids = query('cat ' + [1,2,3,17].map { |index| "/sys/class/block/mmcblk0p#{index}/dev" }.join(' ')).lines.map(&:strip)
      mountinfo = query('cat /proc/self/mountinfo').lines.map(&:split)
      raise Invalid, 'usb_mount_inventory_invalid' unless critical_ids.size == 4 && critical_ids.uniq.size == 4 &&
        critical_ids.all? { |value| value.match?(/\A[0-9]+:[0-9]+\z/) } && !mountinfo.empty? &&
        mountinfo.all? { |fields| fields.size >= 10 && fields[2].match?(/\A[0-9]+:[0-9]+\z/) && (fields.index('-') || 0) >= 6 }
      raise Invalid, 'usb_target_mounted' if mountinfo.any? { |fields| critical_ids.include?(fields[2]) }
      mounted_ids = mountinfo.map { |fields| fields[2] }.uniq
      block_mounts = query("for d in #{mounted_ids.join(' ')}; do if [ -e /sys/dev/block/$d ]; then echo $d; fi; done")
      raise Invalid, 'usb_target_mounted' unless block_mounts.empty?
      mounts = query('cat /proc/mounts')
      raise Invalid, 'usb_target_mounted' if mounts.lines.any? do |line|
        source, point = line.split.first(2)
        %w[/data /metadata].include?(point) || (source &&
          (source.match?(%r{\A/dev/block/(?:mmcblk0p(?:1|2|3|17)|by-name/(?:bootloader|env|boot|UDISK))\z}) ||
           source.start_with?('/dev/block/dm-', '/dev/mapper/')))
      end
      total = A133Backup::BYTES/512
      A133Backup.validate_gpt(range(nil,0,34),range(nil,total-34,34),A133Backup::BYTES,inventory)
      {status: 'recovery_inspection_complete', writes_performed: 0, installation_ready: false}
    rescue A133Backup::Invalid => error
      raise Invalid, error.message
    end

    def backup_valid!(backup)
      hashes = backup.is_a?(Hash) ? backup['partition_sha256'] : nil
      raise Invalid, 'invalid_backup_receipt' unless backup.is_a?(Hash) && backup['cid'] == @cid &&
        backup['status'] == 'backup_integrity_verified' && backup['bytes'] == A133Backup::BYTES &&
        %w[gpt_headers_crc gpt_arrays_crc partition_layout_verified].all? { |key| backup[key] == true } &&
        sha?(backup['uncompressed_sha256']) && hashes.is_a?(Hash) &&
        hashes.keys.sort == %w[boot bootloader env recovery] && hashes.values.all? { |value| sha?(value) }
    end

    def sha?(value)
      value.is_a?(String) && value.match?(/\A[0-9a-f]{64}\z/)
    end

    def protection_valid!(data)
      raise Invalid, 'invalid_protected_environment' unless data.is_a?(String)
      vars = A133Env.decode(data).fetch(:entries).to_h
      profile = A133Recovery::PROFILE.merge('boot_normal'=>'run ember_recovery_once')
      raise Invalid, 'invalid_protected_environment' unless profile.all? { |key,value| vars[key] == value } &&
        vars['ember_recovery_once'] == A133Recovery::UPDATES['ember_recovery_once']
    rescue A133Env::Invalid
      raise Invalid, 'invalid_protected_environment'
    end

    def guard!(backup, env)
      inspect!
      raise Invalid, 'usb_environment_changed' unless range(2,0,256) == env
      raise Invalid, 'usb_recovery_changed' unless range_sha(6,33554432) == backup['partition_sha256']['recovery']
      raise Invalid,'usb_additional_guard_failed' if @additional_guard && @additional_guard.call!=true
    end

    def unchanged!(before, after)
      raise Invalid, 'image_changed' unless [:dev,:ino,:size,:mtime,:ctime].all? do |field|
        before.public_send(field) == after.public_send(field)
      end
    end

    def write_verified(role:, path:, bytes:, sha256:, backup:, protected_env:)
      attempted = false
      raise Invalid, 'invalid_write_role' unless ROLES.key?(role)
      index, maximum = ROLES.fetch(role)
      raise Invalid, 'invalid_write_size' unless bytes.is_a?(Integer) && bytes > 0 && bytes%512 == 0 &&
        bytes <= maximum && (role == 'root' || bytes == maximum)
      raise Invalid, 'invalid_image_sha256' unless sha?(sha256)
      backup_valid!(backup)
      protection_valid!(protected_env)
      # Capture caller-owned policy bytes before any blocking operation.
      env = protected_env.dup.freeze
      backup = Marshal.load(Marshal.dump(backup))
      raise Invalid, 'image_not_regular' unless path.is_a?(String) && File.lstat(path).file?
      File.open(path,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |file|
        before = file.stat
        raise Invalid, 'image_not_regular' unless before.file?
        raise Invalid, 'image_size_mismatch' unless before.size == bytes
        digest = Digest::SHA256.new
        while (chunk = file.read(1048576))
          digest.update(chunk)
        end
        raise Invalid, 'image_sha256_mismatch' unless digest.hexdigest == sha256
        unchanged!(before,file.stat)
        file.rewind
        guard!(backup,env)
        unchanged!(before,file.stat)
        attempted = true
        command = "dd of=/dev/block/mmcblk0p#{index} bs=1048576 conv=notrunc 2>/dev/null && sync"
        transfer = @channel.run(['-s',@serial,'shell','-T',command],source: file,input_bytes: bytes)
        unchanged!(before,file.stat)
        raise Invalid, 'image_changed' unless file.read(1).nil? && transfer[:input_sha256] == sha256
        raise Invalid, 'usb_readback_mismatch' unless range_sha(index,bytes) == sha256
        guard!(backup,env)
      end
      {status: 'range_write_verified', role: role, written_bytes: bytes, sha256: sha256,
       writes_performed: 1, recovery_protection_verified: true, installation_ready: false}
    rescue Invalid => error
      error.write_attempted = attempted
      raise
    rescue SystemCallError, IOError
      error = Invalid.new('image_unavailable')
      error.write_attempted = attempted
      raise error
    end

    def verify_installed(role:, bytes:, sha256:, backup:, protected_env:)
      raise Invalid, 'invalid_write_role' unless ROLES.key?(role)
      index, maximum = ROLES.fetch(role)
      raise Invalid, 'invalid_write_size' unless bytes.is_a?(Integer) && bytes > 0 && bytes%512 == 0 &&
        bytes <= maximum && (role == 'root' || bytes == maximum)
      raise Invalid, 'invalid_image_sha256' unless sha?(sha256)
      backup_valid!(backup)
      protection_valid!(protected_env)
      env = protected_env.dup.freeze
      backup = Marshal.load(Marshal.dump(backup))
      guard!(backup,env)
      raise Invalid, 'usb_readback_mismatch' unless range_sha(index,bytes) == sha256
      guard!(backup,env)
      {status: 'range_readback_verified', role: role, verified_bytes: bytes, sha256: sha256,
       writes_performed: 0, recovery_protection_verified: true, installation_ready: false}
    rescue Invalid => error
      error.write_attempted = false
      raise
    end
    private :query, :range, :range_sha, :backup_valid!, :sha?, :protection_valid!, :guard!, :unchanged!
  end
end

if $PROGRAM_NAME == __FILE__
  result = {writes_performed: 0, installation_ready: false}
  begin
    options = {adb: 'adb', timeout: 600}
    OptionParser.new do |parser|
      parser.on('--adb PATH') { |value| options[:adb] = value }
      parser.on('--serial ID') { |value| options[:serial] = value }
      parser.on('--cid HEX') { |value| options[:cid] = value }
      parser.on('--timeout SECONDS',Integer) { |value| options[:timeout] = value }
    end.parse!
    raise A133Usb::Invalid, 'invalid_arguments' unless ARGV.empty?
    result.merge!(A133Usb::Client.new(**options).inspect!)
    puts JSON.pretty_generate(result)
  rescue A133Usb::Invalid, OptionParser::ParseError, ArgumentError, SystemCallError, IOError => error
    result[:status] = 'inspection_stopped'
    result[:reason] = error.is_a?(A133Usb::Invalid) ? error.message : 'invalid_host_configuration'
    puts JSON.pretty_generate(result)
    exit 1
  end
end
