# Origin: EmberBSD - independent full-size factory env and sparse USB transition fixtures.
require_relative 'a133-usb-test-support'
module RebootFixture
  SERIAL='PROTECTION_TEST_SERIAL'.freeze
  module_function
  def encode(vars,leading=false,pad="\xff".b)
    body=(leading ? "\0".b : ''.b)+vars.map { |key,value| key.b+'='+value.b+"\0" }.join+"\0"
    body=body.ljust(131068,pad); [Zlib.crc32(body)].pack('V')+body
  end
  def create(dir,direct:false)
    UsbFixture.create(dir)
    vars={'opaque'=>"\xffKEEP=this".b+('RETAINED_VALUE'*80),
      'bootcmd'=>'run setargs_mmc boot_normal','boot_normal'=>'run ${hook};run boot_android',
      'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
      'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000','bootdelay'=>'0'}
    helpers={'hook'=>'ember_restore_normal ember_recovery_once',
      'ember_restore_normal'=>'setenv hook; saveenv',
      'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'}
    if direct
      vars.delete('boot_android')
      vars['bootcmd']='run setargs_nand boot_normal'
      vars['boot_normal']='sunxi_flash read 45000000 boot;bootm 45000000'
      vars['setargs_nand']=A133Recovery::DIRECT_SETARGS
      helpers={'ember_entry_restore'=>'setenv bootcmd run setargs_nand boot_normal',
        'ember_entry_save'=>'saveenv',
        'ember_entry_fdt'=>'fdt addr ${fdtcontroladdr}',
        'ember_entry_usb'=>'fdt set /soc@03000000/usbc0@0 usb_port_type <0>'}
    end
    tail=("\0\r\n\xffTAIL_KEEP".b*1400000).byteslice(0,16777216-131072)
    consumed=vars.merge(helpers.reject { |key,_| key=='hook' })
    unlock_consumed=consumed
    if direct
      unlock_consumed=consumed.merge('ember_unlock_flag'=>'pst write fastboot_status_flag unlocked',
        'ember_unlock_next'=>'setenv bootcmd run ember_entry_restore ember_entry_save setargs_nand ember_entry_fdt ember_entry_usb boot_recovery; run ember_entry_save ember_reset',
        'ember_reset'=>'reset')
    end
    armed=vars.merge(helpers)
    armed['bootcmd']='run ember_entry_restore ember_entry_save setargs_nand ember_entry_fdt ember_entry_usb boot_recovery' if direct
    original=encode(vars)+tail
    File.write(File.join(dir,'layout.json'),JSON.generate(UsbFixture::PARTS))
    File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
    disk=File.open(File.join(dir,'disk'),'rb') { |f| head=f.read(17408); f.seek(31037849600-17408); head+f.read(17408) }
    backup=UsbFixture.backup(dir).merge('serial'=>SERIAL,'critical_copies_verified'=>true,
      'hardware_boot_copies_verified'=>true,'gpt_sha256'=>Digest::SHA256.hexdigest(disk))
    backup['partition_sha256']['env']=Digest::SHA256.hexdigest(original)
    %w[bootloader boot recovery].zip([1,3,6]).each { |role,index| backup['partition_sha256'][role]=Digest::SHA256.file(File.join(dir,"part#{index}")).hexdigest }
    context={backup:backup,original:original,armed:encode(armed)+tail,
      model:direct ? 'QUAD-CORE A133 c3' : 'a133',
      consumed:encode(consumed)+tail,unlock_consumed:encode(unlock_consumed)+tail,
      reordered:encode(consumed.to_a.reverse.to_h,true,"\0")+tail,
      altered:encode(consumed.merge('bootdelay'=>'3'))+tail}
    File.binwrite(File.join(dir,'consumed-prefix.bin'),context[:consumed].byteslice(0,131072))
    reset(dir,context)
    context
  end
  def reset(dir,context,state={},live=nil)
    File.binwrite(File.join(dir,'part2'),live || context[:original])
    [1,3,6].each { |index| File.open(File.join(dir,"part#{index}"),'wb') { |f| f.truncate(33554432) } }
    UsbFixture.state(dir,{'model'=>context.fetch(:model,'a133')}.merge(state))
    %w[observed rebooted polls reboot-clock].each { |name| File.unlink(File.join(dir,name)) if File.exist?(File.join(dir,name)) }
  end
end
