# Origin: EmberBSD - independent six-partition mutable backup fixtures, test processes only.
require 'digest'
require 'json'
require 'zlib'
require_relative 'a133-backup-check'
module MutableFixture
  INVENTORY = [
    {index:1,name:'bootloader',start:34,sectors:16},
    {index:2,name:'env',start:50,sectors:16},
    {index:3,name:'boot',start:66,sectors:16},
    {index:4,name:'recovery',start:82,sectors:16},
    {index:5,name:'metadata',start:98,sectors:16},
    {index:6,name:'UDISK',start:114,sectors:201}
  ].freeze
  module_function
  def configure
    A133Backup.send(:remove_const,:BYTES); A133Backup.const_set(:BYTES,262144)
    A133Backup.send(:remove_const,:INVENTORY); A133Backup.const_set(:INVENTORY,INVENTORY)
  end
  def write(directory)
    Dir.mkdir(directory,0700) unless File.directory?(directory)
    data = "\0".b*262144
    data[510,2]="\x55\xaa".b; data.setbyte(450,0xee); data[454,8]=[1,511].pack('V2')
    entries = INVENTORY.each_with_index.map do |part,i|
      'T'*16+(65+i).chr*16+[part[:start],part[:start]+part[:sectors]-1,0].pack('Q<3')+
        part[:name].encode('UTF-16LE').b.ljust(72,"\0")
    end.join
    data[1024,entries.bytesize]=entries; data[479*512,entries.bytesize]=entries
    [1,511].each do |lba|
      header='EFI PART'.b+[0x10000,92,0,0].pack('V4')+[lba,lba==1 ? 511 : 1,34,478].pack('Q<4')+
        'D'*16+[lba==1 ? 2 : 479].pack('Q<')+[6,128,Zlib.crc32(entries)].pack('V3')
      header[16,4]=[Zlib.crc32(header)].pack('V'); data[lba*512,92]=header
    end
    INVENTORY.each_with_index do |part,i|
      bytes = (0..255).map { |j| ((j+i)%256).chr }.join.b*(part[:sectors]*2)
      data[part[:start]*512,bytes.bytesize]=bytes
      name = %w[metadata UDISK].include?(part[:name]) ? part[:name].downcase+'.raw' : part[:name]+'.bin'
      File.binwrite(File.join(directory,name),bytes)
    end
    File.binwrite(File.join(directory,'disk.raw'),data)
    %w[boot0 boot1].each { |role| File.binwrite(File.join(directory,role+'.bin'),'B'*1024) }
    record={'schema'=>1,'board'=>'ys-m33-a133','serial'=>'MUTABLE_TEST_SERIAL',
      'cid'=>'0123456789abcdef0123456789abcdef','gpt_sha256'=>Digest::SHA256.hexdigest(data[0,17408]+data[-17408,17408]),
      'root_method'=>'adbd','capture_state'=>'recovery','observation'=>'recovery_unmounted_two_matching_reads',
      'partitions'=>[{'role'=>'UDISK','file'=>'udisk.raw','bytes'=>102912,'sha256'=>Digest::SHA256.file(File.join(directory,'udisk.raw')).hexdigest},
        {'role'=>'metadata','file'=>'metadata.raw','bytes'=>8192,'sha256'=>Digest::SHA256.file(File.join(directory,'metadata.raw')).hexdigest}]}
    File.write(File.join(directory,'mutable.json'),JSON.generate(record))
    Dir.children(directory).each { |name| File.chmod(0600,File.join(directory,name)) }
    record
  end
end
