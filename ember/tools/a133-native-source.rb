#!/usr/bin/env ruby
# Origin: EmberBSD - pinned native SSH transport for A133 postboot transitions.
require 'base64'
require 'shellwords'
require 'stringio'
require_relative 'a133-cable-install'

module A133Native
  class Source
    attr_reader :host_key_sha256,:kernel_version_sha256
    def initialize(ssh:,host:,host_alias:,known_hosts:,identity:,host_key:,kernel_version:,timeout:120)
      paths=[ssh,known_hosts,identity]
      valid=paths.all? { |value| value.is_a?(String) && value.start_with?('/') && value.valid_encoding? && !value.match?(/[\x00-\x1f\x7f]/) } &&
        host.is_a?(String) && host.match?(/\A[a-zA-Z0-9][a-zA-Z0-9.:%_-]{0,253}\z/) &&
        host_alias.is_a?(String) && host_alias.match?(/\A[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}\z/) &&
        host_key.is_a?(String) && host_key.match?(/\Assh-ed25519 [A-Za-z0-9+\/=]+\z/) &&
        kernel_version.is_a?(String) && kernel_version.encoding.ascii_compatible? && kernel_version.valid_encoding? &&
        kernel_version.bytesize.between?(1,8192) && !kernel_version.include?("\0") &&
        timeout.is_a?(Integer) && timeout.between?(1,7200)
      fail!('native_options_invalid') unless valid
      ssh,host,host_alias,known_hosts,identity,host_key=[ssh,host,host_alias,known_hosts,identity,host_key].map { |value| value.dup.freeze }
      wire=Base64.strict_decode64(host_key.split.last)
      fail!('native_host_key_invalid') unless wire.bytesize==51 && wire.byteslice(0,19)==[11].pack('N')+'ssh-ed25519'+[32].pack('N')
      @host_key_sha256=Digest::SHA256.hexdigest(wire).freeze
      @kernel_version=kernel_version.sub(/\n+\z/,'').freeze
      fail!('native_kernel_version_invalid') if @kernel_version.empty?
      @kernel_version_sha256=Digest::SHA256.hexdigest(@kernel_version).freeze
      @known_hosts=known_hosts.dup.freeze
      @known_line=(host_alias+' '+host_key+"\n").freeze
      safe_file!(identity)
      quoted='"'+known_hosts.gsub(/[\\"]/) { |char| '\\'+char }+'"'
      @arguments=['-F','/dev/null','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes',
        '-o','GlobalKnownHostsFile=/dev/null','-o','UserKnownHostsFile='+quoted,'-o','HostKeyAlias='+host_alias,
        '-o','IdentitiesOnly=yes','-o','ConnectTimeout=5','-o','ConnectionAttempts=1','-o','UpdateHostKeys=no',
        '-i',identity,'root@'+host].map { |value| value.dup.freeze }.freeze
      @channel=A133Usb::Channel.new(ssh.dup.freeze,timeout)
      known_host!
    rescue ArgumentError
      fail!('native_host_key_invalid')
    end

    def epoch
      value=remote(kernel_guard+'test "$(id -u)" = 0; test "$(sysctl -n hw.model)" = allwinner,a133; '+
        '/etc/rc.d/mcu7502_keepalive status >/dev/null; sysctl -n kern.boottime').strip
      fail!('native_boot_epoch_invalid') unless value.ascii_only? && value.bytesize.between?(1,512) && !value.match?(/[\x00\r\n]/)
      value.freeze
    end

    def observe(policy:,boot_sha256:,nonce:)
      fail!('native_options_invalid') unless policy.is_a?(A133Recovery::Policy) &&
        boot_sha256.is_a?(String) && boot_sha256.match?(/\A[0-9a-f]{64}\z/) && nonce.is_a?(String) && nonce.match?(/\A[0-9a-f]{48}\z/)
      boot_sha256=boot_sha256.dup.freeze;nonce=nonce.dup.freeze
      first=epoch
      %w[env boot recovery].each do |name|
        part=A133Backup::INVENTORY.find { |row| row[:name]==name }
        node='dk'+(part[:index]-1).to_s
        expected="#{node} at ld0: #{name}\n#{node}: #{part[:sectors]} blocks at #{part[:start]}, type: ntfs"
        fail!('native_partition_mapping_changed') unless remote("dkctl #{node} getwedgeinfo").strip==expected
        raw='/dev/r'+node
        remote("test -c #{raw}; test ! -L #{raw}")
        fail!('native_device_number_changed') unless remote("stat -f '%Hr:%Lr' #{raw}").strip=="168:#{part[:index]-1}"
      end
      remote('test -c /dev/rld0c; test ! -L /dev/rld0c')
      fail!('native_whole_disk_changed') unless remote("stat -f '%Hr:%Lr' /dev/rld0c").strip=='92:2'
      mounts=remote('mount').lines
      fail!('native_root_mount_changed') unless mounts.any? { |row| row.start_with?('/dev/dk16 on / type ffs (') }
      fail!('native_environment_mounted') if mounts.any? { |row| row.match?(%r{\A/dev/r?dk[125] on }) }
      head=range('/dev/rld0c',34*512,0)
      last=range('/dev/rld0c',34*512,A133Backup::BYTES/512-34)
      A133Backup.validate_gpt(head,last,A133Backup::BYTES,A133Backup::INVENTORY)
      fail!('native_gpt_changed') unless Digest::SHA256.hexdigest(head+last)==policy.data[:gpt_sha]
      fail!('native_boot_image_changed') unless remote('sha256 -q /dev/rdk2').strip==boot_sha256
      fail!('native_recovery_changed') unless remote('sha256 -q /dev/rdk5').strip==policy.data[:recovery_sha]
      marker=remote('if [ ! -e /etc/ember/firstboot-pending ] && [ ! -L /etc/ember/firstboot-pending ]; then exit 0; fi; '+
        'test -f /etc/ember/firstboot-pending; test ! -L /etc/ember/firstboot-pending; '+
        'test "$(stat -f %u:%Lp:%z:%l /etc/ember/firstboot-pending)" = 0:600:49:1; cat /etc/ember/firstboot-pending',limit:49)
      fail!('native_confirmation_marker_changed') unless marker.empty? || marker==nonce+"\n"
      fail!('native_boot_epoch_changed') unless epoch==first
      {epoch:first,marker:marker.empty? ? nil : nonce.dup.freeze}
    rescue A133Backup::Invalid
      fail!('native_gpt_invalid')
    end

    def environment
      range('/dev/rdk1',16777216,0)
    end

    def confirm(nonce:,epoch:)
      fail!('native_options_invalid') unless nonce.is_a?(String) && nonce.match?(/\A[0-9a-f]{48}\z/)
      nonce=nonce.dup.freeze
      guard=epoch_guard(epoch)
      fail!('native_boot_epoch_changed') unless self.epoch==epoch
      remote(guard+'test -f /etc/ember/firstboot-pending; test ! -L /etc/ember/firstboot-pending; '+
        'test "$(stat -f %u:%Lp:%z:%l /etc/ember/firstboot-pending)" = 0:600:49:1; '+
        "test \"$(cat /etc/ember/firstboot-pending)\" = '#{nonce}'; rm /etc/ember/firstboot-pending; sync")
      fail!('native_boot_epoch_changed') unless self.epoch==epoch
      remote('test ! -e /etc/ember/firstboot-pending; test ! -L /etc/ember/firstboot-pending')
      true
    end

    def write_environment(prefix:,epoch:)
      fail!('native_environment_size_mismatch') unless prefix.is_a?(String) && prefix.bytesize==A133Env::SIZE
      prefix=prefix.b.dup.freeze
      A133Env.decode(prefix)
      guard=epoch_guard(epoch)
      fail!('native_boot_epoch_changed') unless self.epoch==epoch
      remote(guard+'dd of=/dev/rdk1 bs=131072 conv=notrunc 2>/dev/null && sync',input:prefix)
      fail!('native_boot_epoch_changed') unless self.epoch==epoch
      true
    rescue A133Env::Invalid
      fail!('invalid_native_environment')
    end

    private
    def fail!(reason)
      raise A133Cable::Invalid,reason,cause:nil
    end
    def epoch_guard(value)
      fail!('native_boot_epoch_invalid') unless value.is_a?(String) && value.ascii_only? &&
        value.bytesize.between?(1,512) && !value.match?(/[\x00\r\n]/)
      kernel_guard+'test "$(sysctl -n kern.boottime)" = '+Shellwords.escape(value)+'; '
    end
    def kernel_guard
      'test "$(uname -v)" = '+Shellwords.escape(@kernel_version)+'; '
    end
    def safe_file!(path)
      stat=File.lstat(path)
      fail!('native_ssh_file_unsafe') unless stat.file? && stat.uid==Process.uid && stat.nlink==1 && stat.mode&077==0
    rescue SystemCallError,IOError
      fail!('native_ssh_file_unavailable')
    end
    def known_host!
      safe_file!(@known_hosts)
      File.open(@known_hosts,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |file|
        stat=file.stat
        fail!('native_ssh_file_unsafe') unless stat.file? && stat.uid==Process.uid && stat.nlink==1 && stat.mode&077==0
        fail!('native_host_key_changed') unless file.read(2049)==@known_line
      end
    rescue SystemCallError,IOError
      fail!('native_ssh_file_unavailable')
    end
    def remote(command,limit:16384,input:nil)
      known_host!
      options={limit:limit}
      options.merge!(source:StringIO.new(input),input_bytes:input.bytesize) if input
      @channel.run(@arguments+['PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH; set -e; '+command],**options)[:output]
    rescue A133Usb::Invalid=>error
      fail!(error.message.sub(/\Ausb_/,'native_'))
    end
    def range(path,bytes,skip)
      value=remote("dd if=#{path} bs=512 skip=#{skip} count=#{bytes/512} 2>/dev/null",limit:bytes)
      fail!('native_short_read') unless value.bytesize==bytes
      value
    end
  end
end
