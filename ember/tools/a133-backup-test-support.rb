# Origin: EmberBSD - independent small GPT test fixtures only.
require 'zlib'
module BackupFixture
  INVENTORY = [{index: 1, start: 34, sectors: 100, name: 'boot'}].freeze
  module_function
  def crc_header(data, lba)
    offset = lba * 512
    header = data.byteslice(offset, 92).dup
    header[16, 4] = "\0" * 4
    data[offset + 16, 4] = [Zlib.crc32(header)].pack('V')
  end
  def disk
    data = "\0".b * 131072
    data[510, 2] = "\x55\xaa".b
    data.setbyte(450, 0xee)
    data[454, 8] = [1, 255].pack('V2')
    entry = 'T' * 16 + 'U' * 16 + [34, 133, 0].pack('Q<3')
    entry << 'boot'.encode('UTF-16LE').b.ljust(72, "\0")
    data[1024, 128] = entry
    data[223 * 512, 128] = entry
    [1, 255].each do |lba|
      header = 'EFI PART'.b + [0x10000, 92, 0, 0].pack('V4')
      header << [lba, lba == 1 ? 255 : 1, 34, 222].pack('Q<4')
      header << 'D' * 16 << [lba == 1 ? 2 : 223].pack('Q<')
      header << [1, 128, Zlib.crc32(entry)].pack('V3')
      data[lba * 512, 92] = header
      crc_header(data, lba)
    end
    data[34 * 512, 51200] = 'Q' * 51200
    data
  end
  def arrays_crc(data)
    [1, 255].each do |lba|
      array_lba = lba == 1 ? 2 : 223
      data[lba * 512 + 88, 4] = [Zlib.crc32(data.byteslice(array_lba * 512, 128))].pack('V')
      crc_header(data, lba)
    end
  end
end

