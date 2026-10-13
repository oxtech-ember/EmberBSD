# Origin: EmberBSD - shared test-only sparse GPT/device fixture helpers.
require 'digest'
require 'json'
require 'zlib'
module UsbFixture
  CID = '0123456789abcdef0123456789abcdef'
  PARTS = [
    ['bootloader',73728,65536], ['env',139264,32768], ['boot',172032,65536],
    ['super',237568,4194304], ['misc',4431872,32768], ['recovery',4464640,65536],
    ['cache',4530176,1572864], ['vbmeta',6103040,32768], ['vbmeta_system',6135808,32768],
    ['vbmeta_vendor',6168576,32768], ['metadata',6201344,32768], ['private',6234112,32768],
    ['frp',6266880,1024], ['empty',6267904,31744], ['dtbo',6299648,4096],
    ['media_data',6303744,262144], ['UDISK',6565888,54054879]
  ].freeze
  module_function
  def header(current, alternate, array_lba, crc)
    data = 'EFI PART'.b + [0x10000,92,0,0].pack('V4') +
      [current,alternate,73728,60620766].pack('Q<4') + 'D' * 16 +
      [array_lba].pack('Q<') + [17,128,crc].pack('V3')
    data[16,4] = [Zlib.crc32(data)].pack('V')
    data.ljust(512, "\0")
  end
  def env
    vars = {
      'bootcmd'=>'run setargs_mmc boot_normal', 'boot_normal'=>'run ember_recovery_once',
      'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
      'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000',
      'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'
    }
    body = (vars.map { |key,value| "#{key}=#{value}\0" }.join + "\0").ljust(131068,"\0")
    [Zlib.crc32(body)].pack('V') + body
  end
  def create(dir)
    entries = PARTS.each_with_index.map do |(name,start,size),i|
      'T' * 16 + [i+1].pack('V').ljust(16,"G") + [start,start+size-1,0].pack('Q<3') +
        name.encode('UTF-16LE').b.ljust(72,"\0")
    end.join
    File.open(File.join(dir,'disk'), 'wb') do |file|
      file.truncate(31037849600)
      mbr = "\0" * 512
      mbr[446,16] = [0,0,0,0,0xee,0,0,0].pack('C8') + [1,60620799].pack('V2')
      mbr[510,2] = "\x55\xaa".b
      file.write(mbr); file.write(header(1,60620799,2,Zlib.crc32(entries))); file.write(entries)
      file.seek(60620794*512); file.write(entries)
      file.seek(60620799*512); file.write(header(60620799,1,60620794,Zlib.crc32(entries)))
    end
    [1,2,3,6,17].each do |index|
      File.open(File.join(dir,"part#{index}"),'wb') { |file| file.truncate(PARTS[index-1][2]*512) }
    end
    File.open(File.join(dir,'part2'),'r+b') { |file| file.write(env) }
    File.open(File.join(dir,'part17'),'r+b') { |file| file.seek(2048); file.write('TAIL_PRESERVED') }
    File.binwrite(File.join(dir,'source'), ("\0\r\n\xffABCD".b * 128))
    File.write(File.join(dir,'adb'), File.read(File.join(__dir__, 'a133-usb-transfer-fixture.rb')))
    File.chmod(0700, File.join(dir,'adb'))
  end
  def state(dir, updates={})
    File.write(File.join(dir,'state.json'), JSON.generate(updates))
    File.unlink(File.join(dir,'written')) if File.exist?(File.join(dir,'written'))
  end
  def backup(dir)
    {'status'=>'backup_integrity_verified', 'bytes'=>31037849600, 'cid'=>CID,
     'uncompressed_sha256'=>'a'*64, 'gpt_headers_crc'=>true, 'gpt_arrays_crc'=>true,
     'partition_layout_verified'=>true, 'partition_sha256'=>{
       'bootloader'=>'b'*64,'env'=>'c'*64,'boot'=>'d'*64,
       'recovery'=>Digest::SHA256.file(File.join(dir,'part6')).hexdigest}}
  end
end
