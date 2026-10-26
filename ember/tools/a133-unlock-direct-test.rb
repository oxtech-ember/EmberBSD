#!/usr/bin/env ruby
# Origin: EmberBSD - independent direct-factory unlock ordering and byte contracts.
require 'zlib'
require 'tmpdir'
require 'open3'
require 'rbconfig'
require_relative 'a133-unlock-env'
vars={'bootcmd'=>'run setargs_nand boot_normal',
  'boot_normal'=>'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000',
  'setargs_nand'=>'setenv bootargs retained_args','opaque'=>"\xffKEEP;value=this".b,'empty'=>'','bootdelay'=>'0'}
updates={'ember_entry_restore'=>'setenv bootcmd run setargs_nand boot_normal',
  'ember_entry_save'=>'saveenv','ember_entry_fdt'=>'fdt addr ${fdtcontroladdr}',
  'ember_entry_usb'=>'fdt set /soc@03000000/usbc0@0 usb_port_type <0>',
  'ember_unlock_flag'=>'pst write fastboot_status_flag unlocked',
  'ember_unlock_next'=>'setenv bootcmd run ember_entry_restore ember_entry_save setargs_nand ember_entry_fdt ember_entry_usb boot_recovery; run ember_entry_save ember_reset',
  'ember_reset'=>'reset','bootcmd'=>'run ember_unlock_flag ember_unlock_next'}
encode=lambda do |entries,leading=false,pad="\xff".b|
  body=(leading ? "\0".b : ''.b)+entries.map { |k,v| k.b+'='+v.b+"\0" }.join+"\0"
  body=body.ljust(131068,pad); [Zlib.crc32(body)].pack('V')+body
end
checks=0
[false,true].product(["\0".b,"\xff".b]).each do |leading,pad|
  original=encode.call(vars,leading,pad);saved=original.dup
  candidate=A133Unlock.prepare(original)
  abort 'FAIL: direct unlock ordering or unrelated bytes' unless candidate==encode.call(vars.merge(updates),leading,pad) && original==saved
  reverse=updates.transform_values { '' }.merge('bootcmd'=>vars['bootcmd'])
  abort 'FAIL: direct unlock is not reversible' unless A133Env.patch(candidate,reverse)==original
  checks+=1
end
reject=lambda do |entries|
  begin
    A133Unlock.prepare(encode.call(entries));abort 'FAIL: unsafe direct unlock accepted'
  rescue A133Unlock::Invalid
  end
  checks+=1
end
(updates.keys-['bootcmd']+%w[hook boot_android ember_save_env ember_restore_normal ember_recovery_once]).each do |key|
  ['', 'foreign'].each { |value| reject.call(vars.merge(key=>value)) }
end
%w[bootcmd boot_normal boot_recovery setargs_nand].each do |key|
  reject.call(vars.reject { |k,_| k==key });reject.call(vars.merge(key=>'run foreign')) unless key=='setargs_nand'
end
reject.call(vars.merge('setargs_nand'=>''))
Dir.mktmpdir('a133-direct-unlock-') do |dir|
  original=encode.call(vars);input=File.join(dir,'original');output=File.join(dir,'candidate')
  File.binwrite(input,original)
  out,err,status=Open3.capture3(RbConfig.ruby,File.join(__dir__,'a133-unlock-env.rb'),input,output)
  result=JSON.parse(out)
  abort 'FAIL: direct CLI output, keys or input' unless status.success? && err.empty? &&
    result['status']=='unlock_copy_prepared' && result['changed_variables']==updates.keys.sort &&
    result['device_writes_performed']==0 && File.binread(output)==encode.call(vars.merge(updates)) && File.binread(input)==original
  checks+=1
end
puts "Direct unlock plan: #{checks} byte, ordering and refusal cases passed"
