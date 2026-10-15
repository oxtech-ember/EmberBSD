# Origin: EmberBSD - independent four-partition capture fixture, test process only.
require 'digest'
require 'json'
require 'zlib'
require_relative 'a133-backup-check'

module CaptureFixture
  INVENTORY = [
    {index:1, name:'bootloader', start:34, sectors:16},
    {index:2, name:'env', start:50, sectors:16},
    {index:3, name:'boot', start:66, sectors:16},
    {index:4, name:'recovery', start:82, sectors:16}
  ].freeze
  module_function
  def configure
    A133Backup.send(:remove_const, :BYTES)
    A133Backup.const_set(:BYTES, 262144)
    A133Backup.send(:remove_const, :INVENTORY)
    A133Backup.const_set(:INVENTORY, INVENTORY)
  end
  def data
    bytes = "\0".b * 262144
    bytes[510,2] = "\x55\xaa".b
    bytes.setbyte(450,0xee)
    bytes[454,8] = [1,511].pack('V2')
    entries = INVENTORY.each_with_index.map do |part,i|
      'T'*16 + (65+i).chr*16 + [part[:start],part[:start]+15,0].pack('Q<3') +
        part[:name].encode('UTF-16LE').b.ljust(72,"\0")
    end.join
    bytes[1024,512] = entries
    bytes[479*512,512] = entries
    [1,511].each do |lba|
      header = 'EFI PART'.b + [0x10000,92,0,0].pack('V4') +
        [lba,lba==1 ? 511 : 1,34,478].pack('Q<4') + 'D'*16 +
        [lba==1 ? 2 : 479].pack('Q<') + [4,128,Zlib.crc32(entries)].pack('V3')
      header[16,4] = [Zlib.crc32(header)].pack('V')
      bytes[lba*512,92] = header
    end
    INVENTORY.each_with_index { |part,i| bytes[part[:start]*512,8192] = (97+i).chr*8192 }
    bytes
  end
  def write(dir)
    File.chmod(0700,dir)
    bytes = data
    File.binwrite(File.join(dir,'disk.raw'),bytes)
    INVENTORY.each do |part|
      File.binwrite(File.join(dir,part[:name]+'.bin'),bytes.byteslice(part[:start]*512,8192))
    end
    manifest = {'schema'=>1,'board'=>'ys-m33-a133','serial'=>'CAPTURE_TEST_SERIAL',
      'cid'=>'0123456789abcdef0123456789abcdef',
      'uncompressed_sha256'=>Digest::SHA256.hexdigest(bytes),
      'backup'=>{'file'=>'disk.raw','format'=>'raw'},
      'partitions'=>INVENTORY.map { |part| {'role'=>part[:name],'file'=>part[:name]+'.bin'} }}
    File.write(File.join(dir,'capture.json'),JSON.generate(manifest))
    Dir.children(dir).each { |name| File.chmod(0600,File.join(dir,name)) }
    manifest
  end
end
