#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require 'fileutils'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require_relative 'ffs1-check'

abort 'usage: ruby ffs1-check-test.rb /absolute/nbmakefs' unless ARGV.length == 1 && ARGV[0].start_with?('/')
makefs = ARGV[0]

def check(value, message)
  raise message unless value
end

def accepted(image, offset, wanted)
  reader = FFS1Check.new(image, offset)
  begin
    check(reader.verify(wanted) == wanted.size, 'wrong verified count')
  ensure
    reader.close
  end
end

def rejected(image, offset, wanted)
  begin
    accepted(image, offset, wanted)
  rescue FFS1Check::Error
    return
  end
  raise 'negative control was accepted'
end

def mutate(image, at, bytes)
  File.open(image, 'r+b') do |file|
    file.seek(at)
    old = file.read(bytes.bytesize)
    file.seek(at)
    file.write(bytes)
    file.flush
    begin
      yield
    ensure
      file.seek(at)
      file.write(old)
    end
  end
end

Dir.mktmpdir('ffs1-read-check-') do |work|
  [[4096, 512, 0], [8192, 1024, 1048576], [65536, 8192, 301989888]].each_with_index do |(block, frag, offset), index|
    tree = File.join(work, "tree#{index}")
    dirs = { '/' => 0755, '/etc' => 0755, '/etc/ssh' => 0755, '/root' => 0700,
             '/root/.ssh' => 0700, '/many' => 0755 }
    wanted = []
    spec = []
    dirs.each do |path, mode|
      FileUtils.mkdir_p(tree + path)
      spec << ".#{path == '/' ? '' : path} type=dir uid=0 gid=0 mode=#{format('%04o', mode)}"
      wanted << { 'path' => path, 'type' => 'directory', 'uid' => 0, 'gid' => 0, 'mode' => format('%04o', mode) }
    end
    files = { '/etc/rc.conf' => [0640, 0, 0, "fixture rc\n"],
              '/etc/ssh/sshd_config' => [0600, 0, 0, "fixture ssh\n"],
              '/etc/wpa_supplicant.conf' => [0600, 0, 0, "synthetic private fixture\0\xff".b],
              '/root/.ssh/authorized_keys' => [0600, 0, 0, "fixture authorized key\n"],
              '/etc/ssh/ssh_host_ed25519_key' => [0600, 0, 0, "fixture host key\n"],
              '/many/data' => [0644, 70001, 80002, (0..255).to_a.pack('C*') * ((block * 12) / 256) ],
              '/many/empty' => [0644, 0, 0, ''] }
    files['/many/data'][3] = files['/many/data'][3].byteslice(0, block * 12 - 17)
    files.each do |path, (mode, uid, gid, data)|
      File.binwrite(tree + path, data)
      spec << ".#{path} type=file uid=#{uid} gid=#{gid} mode=#{format('%04o', mode)}"
      wanted << { 'path' => path, 'type' => 'file', 'uid' => uid, 'gid' => gid,
                  'mode' => format('%04o', mode), 'source' => tree + path }
    end
    File.symlink('data', tree + '/many/link')
    spec << './many/link type=link uid=0 gid=0 mode=0777 link=data'
    File.binwrite(tree + '/many/indirect', 'z' * (block * 12 + 1))
    spec << './many/indirect type=file uid=0 gid=0 mode=0644'
    # Force multiple directory sectors and multiple inode blocks.
    250.times do |i|
      path = "/many/entry#{format('%03d', i)}"
      File.write(tree + path, 'x')
      spec << ".#{path} type=file uid=0 gid=0 mode=0644"
    end
    wanted << { 'path' => '/many/entry249', 'type' => 'file', 'uid' => 0, 'gid' => 0, 'mode' => '0644', 'source' => tree + '/many/entry249' }
    specfile = File.join(work, "spec#{index}")
    File.write(specfile, spec.join("\n") + "\n")
    image = File.join(work, "image#{index}")
    geometry = "v=1,b=#{block},f=#{frag},d=4096" + (index == 0 ? ',g=512' : '')
    command = [makefs, '-B', 'le', '-t', 'ffs', '-o', geometry,
               '-s', '16m', '-O', offset.to_s, '-F', specfile, image, tree]
    output, state = Open3.capture2e(*command)
    check(state.success?, "nbmakefs fixture failed: #{output}")
    before = File.stat(image)
    accepted(image, offset, wanted)
    if index == 0
      reader = FFS1Check.new(image, offset)
      begin
        node = reader.send(:lookup, '/many/entry249')
        check(node[:number] >= reader.instance_variable_get(:@ipg), 'fixture did not cross a cylinder group')
      ensure
        reader.close
      end
    end
    check([before.size, before.mtime, before.ctime] == [File.size(image), File.mtime(image), File.ctime(image)], 'reader changed image metadata')
    %w[link indirect].each do |name|
      entry = { 'path' => '/many/' + name, 'type' => 'file', 'uid' => 0, 'gid' => 0,
                'mode' => '0644', 'source' => tree + '/many/' + name }
      rejected(image, offset, wanted + [entry])
    end
    expectation_file = File.join(work, "wanted#{index}.json")
    File.write(expectation_file, JSON.generate(wanted))
    result, state = Open3.capture2e(RbConfig.ruby, File.join(__dir__, 'ffs1-check.rb'),
                                  '--image', image, '--offset', offset.to_s, '--expect', expectation_file)
    check(state.success? && result.start_with?('PASS:'), 'CLI check failed')
    check(!result.include?('synthetic private fixture') && !result.include?('fixture host key'), 'CLI printed file contents')
    %w[uid gid mode].each do |field|
      changed = wanted.map(&:dup)
      changed[0][field] = field == 'mode' ? '0700' : 999
      rejected(image, offset, changed)
    end
    content_path = tree + '/etc/rc.conf'
    original = File.binread(content_path)
    File.binwrite(content_path, 'X' * original.bytesize)
    rejected(image, offset, wanted)
    File.binwrite(content_path, original)
    rejected(image, offset + 512, wanted)
    mutate(image, offset + 8192 + 1372, [0].pack('V')) { rejected(image, offset, wanted) }
    mutate(image, offset + 8192 + 1324, [1].pack('V')) { rejected(image, offset, wanted) }
    mutate(image, offset + 8192 + 48, [3].pack('V')) { rejected(image, offset, wanted) }
    File.open(image, 'rb') do |file|
      file.seek(offset + 8192 + 16)
      inode_start = file.read(4).unpack1('V') * frag + 2 * 128
      file.seek(offset + inode_start + 40)
      dir_start = file.read(4).unpack1('V') * frag
      mutate(image, offset + inode_start + 40, [0xffffffff].pack('V')) { rejected(image, offset, wanted) }
      mutate(image, offset + inode_start + 88, [1].pack('V')) { rejected(image, offset, wanted) }
      mutate(image, offset + dir_start + 4, [0].pack('v')) { rejected(image, offset, wanted) }
      # A validly aligned first record can leave only four bytes in a sector.
      # This must raise the checker's bounded error, not a Ruby unpack error.
      mutate(image, offset + dir_start + 4, [508].pack('v')) { rejected(image, offset, wanted) }
    end
    accepted(image, offset, wanted)
    puts "PASS: real nbmakefs FFS1 b=#{block} f=#{frag} offset=#{offset}, nested metadata/bytes and rejection controls"
  end
end
