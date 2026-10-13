#!/usr/bin/env ruby
# Origin: EmberBSD - recovery holder availability before/after actual env restoration.
require 'tmpdir'
require_relative 'a133-usb-test-support'
require_relative 'a133-recovery-entry'
serial='PROTECTION_TEST_SERIAL'
vars={'bootcmd'=>'run setargs_mmc boot_normal','boot_normal'=>'run ${hook};run boot_android',
  'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000'}
helpers={'hook'=>'ember_restore_normal ember_recovery_once',
  'ember_restore_normal'=>'setenv hook; saveenv',
  'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'}
encode=lambda do |values|
  body=(values.map { |key,value| key+'='+value+"\0" }.join+"\0").ljust(131068,"\0")
  [Zlib.crc32(body)].pack('V')+body
end
tail='TAIL'*4161536
original=encode.call(vars)+tail; armed=encode.call(vars.merge(helpers))+tail
checks=0; failures=[]
Dir.mktmpdir('a133-entry-holders-') do |dir|
  UsbFixture.create(dir)
  File.write(File.join(dir,'layout.json'),JSON.generate(UsbFixture::PARTS))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  gpt=File.open(File.join(dir,'disk'),'rb') { |f| head=f.read(17408); f.seek(31037849600-17408); head+f.read(17408) }
  backup=UsbFixture.backup(dir).merge('serial'=>serial,'critical_copies_verified'=>true,
    'hardware_boot_copies_verified'=>true,'gpt_sha256'=>Digest::SHA256.hexdigest(gpt))
  backup['partition_sha256']['env']=Digest::SHA256.hexdigest(original)
  backup['partition_sha256']['bootloader']=Digest::SHA256.file(File.join(dir,'part1')).hexdigest
  [[false,{'holders_unreadable'=>true}],[true,{'write_mode'=>'late_holders_unreadable'}]].each do |attempted,state|
    File.binwrite(File.join(dir,'part2'),armed); UsbFixture.state(dir,state)
    begin
      result=A133Recovery::Entry.new(adb:File.join(dir,'adb'),serial:serial,cid:UsbFixture::CID,
        state:'recovery',root_method:'adbd').restore(original_env:original,backup:backup)
      failures << "unsafe unavailable holders accepted (after=#{attempted}, writes=#{result[:writes_performed]})"
    rescue A133Usb::Invalid => error
      if error.message=='usb_command_failed' && error.write_attempted==attempted && error.cause.nil? &&
          !error.full_message.include?(dir) && !error.full_message.include?('PRIVATE_')
        checks+=1
      else
        failures << "wrong holder failure boundary (after=#{attempted}, #{error.message}/#{error.write_attempted.inspect})"
      end
    end
    expected=attempted ? original : armed
    failures << "incorrect actual write boundary (after=#{attempted})" unless File.binread(File.join(dir,'part2'))==expected &&
      File.exist?(File.join(dir,'written'))==attempted
  end
end
abort failures.map { |failure| 'FAIL: '+failure }.join("\n") unless failures.empty?
puts "Recovery entry holders: #{checks} before/after actual-write regressions passed"
