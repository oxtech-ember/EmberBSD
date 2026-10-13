#!/usr/bin/env ruby
# Origin: EmberBSD - real-write system I/O failure boundary regressions.
require 'rbconfig'
require 'tmpdir'
require 'zlib'
require_relative 'a133-usb-test-support'
require_relative 'a133-recovery-protection'
checks=0
Dir.mktmpdir('a133-protection-io-') do |dir|
  UsbFixture.create(dir)
  vars={
    'bootcmd'=>'run setargs_mmc boot_normal','boot_normal'=>'run ${hook};run boot_android',
    'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
    'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000'
  }
  body=(vars.map { |key,value| "#{key}=#{value}\0" }.join+"\0").ljust(131068,"\0")
  original=[Zlib.crc32(body)].pack('V')+body+('TAIL'*4161536)
  abort 'FAIL: original fixture size' unless original.bytesize==16777216
  File.binwrite(File.join(dir,'part2'),original)
  File.write(File.join(dir,'layout.json'),JSON.generate(UsbFixture::PARTS))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  serial='PROTECTION_TEST_SERIAL'
  backup=UsbFixture.backup(dir).merge('serial'=>serial,'critical_copies_verified'=>true,
    'hardware_boot_copies_verified'=>true)
  backup['partition_sha256']['env']=Digest::SHA256.hexdigest(original)
  gpt=File.open(File.join(dir,'disk'),'rb') do |file|
    head=file.read(17408); file.seek(31037849600-17408); head+file.read(17408)
  end
  mutable={'status'=>'mutable_integrity_verified','serial'=>serial,'cid'=>UsbFixture::CID,
    'gpt_sha256'=>Digest::SHA256.hexdigest(gpt),'mutable_copies_verified'=>true,
    'observation'=>'recovery_unmounted_two_matching_reads',
    'filesystem_consistency'=>'not_established_by_integrity_check',
    'partition_sha256'=>{'UDISK'=>'e'*64,'metadata'=>'f'*64}}
  launch=File.join(dir,'launch')
  (ARGV==['--after-write'] ? [true] : [false,true]).each do |after_write|
    UsbFixture.state(dir,after_write ? {'write_mode'=>'launcher_invalid'} : {})
    if after_write
      File.unlink(launch) if File.exist?(launch)
      Dir.mkdir(launch)
      File.write(File.join(launch,'adb'),"#!#{RbConfig.ruby}\nexec #{File.join(dir,'adb').inspect}, *ARGV\n")
      File.chmod(0700,File.join(launch,'adb'))
    else
      File.write(launch,'PRIVATE_HOST_LOCATION')
    end
    begin
      A133Recovery::Protection.new(adb:File.join(launch,'adb'),serial:serial,cid:UsbFixture::CID).
        install(original_env:original,backup:backup,mutable:mutable)
      abort 'FAIL: system error accepted'
    rescue A133Usb::Invalid => error
      abort 'FAIL: wrong normalized I/O failure' unless error.message=='usb_protection_io_failed' &&
        error.write_attempted==after_write && error.cause.nil? && !error.full_message.include?(dir)
    end
    live=File.binread(File.join(dir,'part2'))
    if after_write
      live_vars=A133Env.decode(live.byteslice(0,131072))[:entries].to_h
      abort 'FAIL: regression did not write expected env' unless live_vars==vars.merge(
        'boot_normal'=>'run ember_recovery_once',
        'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery') &&
        live.byteslice(131072,16646144)==original.byteslice(131072,16646144) &&
        File.file?(File.join(dir,'written'))
    else
      abort 'FAIL: pre-submission error changed env' unless live==original && !File.exist?(File.join(dir,'written'))
    end
    checks+=1
  end
end
puts "Recovery protection I/O: #{checks} before/after actual-write regressions passed"
