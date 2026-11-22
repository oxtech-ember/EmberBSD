#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
# Bounded, read-only verification of small files in a generated FFS1 image.
require 'json'
require 'optparse'

class FFS1Check
  class Error < StandardError; end
  # OS sys/ufs/ffs/fs.h: struct fs, SBLOCK_UFS1, FS_UFS1_MAGIC,
  # FS_44INODEFMT, cgstart_ufs1, cgimin, ino_to_fsba, ino_to_fsbo.
  SBLOCK = 8192
  SUPER_SIZE = 1376
  MAGIC = 0x011954
  # OS sys/ufs/ufs/dinode.h: struct ufs1_dinode (128 bytes), UFS_NDADDR.
  INODE_SIZE = 128
  DIRECT_BLOCKS = 12
  MAX_ENTRIES = 64

  def initialize(image, offset)
    @io = File.open(image, 'rb')
    @before = @io.stat
    require_value(@before.file?, 'image must be a regular file')
    require_value(offset.is_a?(Integer) && offset >= 0 && offset % 512 == 0, 'invalid filesystem offset')
    @offset = offset
    @extent = @before.size - offset
    sb = read_at(SBLOCK, SUPER_SIZE)
    require_value(u32(sb, 1372) == MAGIC, 'requires little-endian FFS1')
    require_value(u32(sb, 1324) == 2, 'requires 4.4BSD inode format')
    @iblk, @cgoffset, @cgmask = [16, 24, 28].map { |at| u32(sb, at) }
    @fragments, @ncg, @bsize, @fsize, @frag = [36, 44, 48, 52, 56].map { |at| u32(sb, at) }
    @inopb, @ipg, @fpg = [120, 184, 188].map { |at| u32(sb, at) }
    require_value(power_two?(@bsize) && @bsize.between?(4096, 65536) &&
                  power_two?(@fsize) && @fsize.between?(512, [8192, @bsize].min) &&
                  [1, 2, 4, 8].include?(@frag) && @frag == @bsize / @fsize,
                  'unsupported filesystem geometry')
    require_value(@inopb == @bsize / INODE_SIZE && u32(sb, 116) == @bsize / 4 &&
                  @ncg > 0 && @ipg > 0 && @ipg % @inopb == 0 && @fpg > 0 &&
                  @iblk > 0 && @iblk < @fpg && @fragments > 0 &&
                  @ncg == (@fragments + @fpg - 1) / @fpg,
                  'invalid inode geometry')
    require_value(@fragments * @fsize <= @extent, 'filesystem exceeds image bounds')
    @extent = @fragments * @fsize
    require_value(SBLOCK + SUPER_SIZE <= @extent, 'truncated filesystem')
  rescue StandardError
    @io.close if @io && !@io.closed?
    raise
  end

  def close
    @io.close
  end

  def verify(expectations)
    require_value(expectations.is_a?(Array) && expectations.size.between?(1, MAX_ENTRIES), 'invalid expectation list')
    seen = {}
    expectations.each do |item|
      require_value(item.is_a?(Hash) && (item.keys - %w[path type uid gid mode source]).empty?, 'invalid expectation fields')
      path = item['path']
      require_value(path.is_a?(String) && path.start_with?('/') && !seen[path], 'invalid or duplicate expected path')
      seen[path] = true
      require_value(item['mode'].is_a?(String) && item['mode'].match?(/\A[0-7]{4}\z/), 'invalid expected mode')
      require_value(%w[file directory].include?(item['type']), 'unsupported expected type')
      %w[uid gid].each { |key| require_value(item[key].is_a?(Integer) && item[key].between?(0, 0xffffffff), 'invalid expected owner') }
      node = lookup(path)
      kind = item['type'] == 'file' ? 0100000 : 0040000
      require_value(node[:mode] & 0170000 == kind, 'inode type differs')
      require_value(node[:mode] & 07777 == item['mode'].to_i(8), 'inode mode differs')
      require_value(node[:uid] == item['uid'] && node[:gid] == item['gid'], 'inode owner differs')
      if kind == 0100000
        source = item['source']
        require_value(source.is_a?(String) && source.start_with?('/'), 'file comparison requires an absolute source')
        File.open(source, 'rb') do |expected|
          before = expected.stat
          require_value(before.file? && before.size == node[:size], 'file size differs')
          require_value(contents(node) == expected.read(node[:size]), 'file bytes differ')
          require_value(stamp(before) == stamp(expected.stat), 'expected file changed during verification')
        end
      else
        require_value(!item.key?('source'), 'directory cannot have a content source')
      end
    end
    require_value(stamp(@before) == stamp(@io.stat), 'image changed during verification')
    expectations.length
  end

  private

  def require_value(value, message)
    raise Error, message unless value
  end

  def stamp(stat)
    [stat.dev, stat.ino, stat.size, stat.mtime, stat.ctime]
  end

  def power_two?(value)
    value > 0 && (value & (value - 1)) == 0
  end

  def u32(bytes, offset)
    bytes.byteslice(offset, 4).unpack1('V')
  end

  def read_at(offset, length)
    require_value(offset >= 0 && length >= 0 && offset <= @extent && length <= @extent - offset, 'read exceeds filesystem bounds')
    @io.seek(@offset + offset)
    bytes = @io.read(length)
    require_value(bytes && bytes.bytesize == length, 'truncated image read')
    bytes
  end

  def inode(number)
    require_value(number >= 2 && number < @ncg * @ipg, 'invalid inode number')
    cg = number / @ipg
    base = cg * @fpg + @cgoffset * (cg & (~@cgmask & 0xffffffff)) + @iblk
    address = base + ((number % @ipg) / @inopb) * @frag
    raw = read_at(address * @fsize + (number % @inopb) * INODE_SIZE, INODE_SIZE)
    node = { number: number, mode: raw.unpack1('v'), size: raw.byteslice(8, 8).unpack1('Q<'),
             uid: u32(raw, 112), gid: u32(raw, 116), raw: raw }
    require_value([0040000, 0100000].include?(node[:mode] & 0170000), 'only directories and regular files are supported')
    require_value(node[:size] <= DIRECT_BLOCKS * @bsize, 'indirect file data is unsupported')
    require_value(raw.byteslice(88, 12).unpack('V3').all?(&:zero?), 'indirect block pointers are unsupported')
    node
  end

  def contents(node)
    result = ''.b
    blocks = (node[:size] + @bsize - 1) / @bsize
    blocks.times do |index|
      fragment = u32(node[:raw], 40 + index * 4)
      require_value(fragment > 0, 'sparse data is unsupported')
      length = [@bsize, node[:size] - result.bytesize].min
      result << read_at(fragment * @fsize, length)
    end
    result
  end

  def lookup(path)
    parts = path == '/' ? [] : path.byteslice(1..-1).split('/', -1)
    require_value(parts.length <= 32 && parts.all? { |p| !['', '.', '..'].include?(p) && !p.include?("\0") }, 'invalid path components')
    node = inode(2)
    parts.each do |part|
      require_value(node[:mode] & 0170000 == 0040000, 'path component is not a directory')
      data = contents(node)
      require_value(data.bytesize % 512 == 0, 'invalid directory size')
      found = nil
      at = 0
      # OS sys/ufs/ufs/dir.h: struct direct, UFS_DIRBLKSIZ, UFS_DIRECTSIZ.
      while at < data.bytesize
        require_value(data.bytesize - at >= 8 && 512 - at % 512 >= 8, 'truncated directory record header')
        number, reclen, type, namlen = data.byteslice(at, 8).unpack('VvCC')
        require_value(reclen >= 8 && reclen % 4 == 0 && reclen <= 512 - at % 512, 'invalid directory record length')
        if number != 0
          require_value(namlen > 0 && reclen >= 8 + ((namlen + 4) & ~3), 'invalid directory name length')
          name = data.byteslice(at + 8, namlen)
          require_value(data.getbyte(at + 8 + namlen) == 0 && !name.include?("\0") && !name.include?('/'), 'invalid directory name')
          if name == part.b
            require_value(found.nil?, 'duplicate directory entry')
            found = [number, type]
          end
        end
        at += reclen
      end
      require_value(found, 'expected path is absent')
      node = inode(found[0])
      require_value(found[1] == 0 || found[1] == ((node[:mode] & 0170000) >> 12), 'directory type differs from inode')
    end
    node
  end
end

if $PROGRAM_NAME == __FILE__
  options = {}
  parser = OptionParser.new do |p|
    p.banner = 'Usage: ruby ffs1-check.rb --image FILE --offset BYTES --expect PRIVATE_JSON'
    p.on('--image FILE') { |v| options[:image] = v }
    p.on('--offset BYTES', Integer) { |v| options[:offset] = v }
    p.on('--expect FILE') { |v| options[:expect] = v }
  end
  begin
    parser.parse!
    raise FFS1Check::Error, 'select image, offset and expectations' unless options.size == 3 && ARGV.empty?
    encoded = File.binread(options[:expect], 1024 * 1024 + 1)
    raise FFS1Check::Error, 'expectation input is too large' if encoded.bytesize > 1024 * 1024
    expectations = JSON.parse(encoded)
    reader = FFS1Check.new(options[:image], options[:offset])
    begin
      count = reader.verify(expectations)
      puts "PASS: FFS1 metadata and file bytes (#{count} entries)"
    ensure
      reader.close
    end
  rescue FFS1Check::Error => error
    warn "FAIL: #{error.message}"
    exit 1
  rescue SystemCallError, IOError, JSON::ParserError, OptionParser::ParseError
    warn 'FAIL: cannot read or parse the selected inputs'
    exit 1
  end
end
