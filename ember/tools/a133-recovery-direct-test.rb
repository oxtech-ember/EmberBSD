#!/usr/bin/env ruby
# Origin: EmberBSD - independent direct-boot factory entry byte contracts.
require 'zlib'
require_relative 'a133-recovery-env'

vars={'bootcmd'=>'run setargs_nand boot_normal',
  'boot_normal'=>'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000',
  'setargs_nand'=>'setenv bootargs retained_args','opaque'=>"\xffKEEP;value=this".b,'empty'=>'','bootdelay'=>'0'}
helpers={'ember_entry_restore'=>'setenv bootcmd run setargs_nand boot_normal',
  'ember_entry_save'=>'saveenv',
  'ember_entry_fdt'=>'fdt addr ${fdtcontroladdr}',
  'ember_entry_usb'=>'fdt set /soc@03000000/usbc0@0 usb_port_type <0>',
}
armed_cmd='run ember_entry_restore ember_entry_save setargs_nand ember_entry_fdt ember_entry_usb boot_recovery'
encode=lambda do |entries,leading,pad|
  body=(leading ? "\0".b : ''.b)+entries.map { |k,v| k.b+'='+v.b+"\0" }.join+"\0"
  body=body.ljust(131068,pad); [Zlib.crc32(body)].pack('V')+body
end
abort 'FAIL: direct entry planner missing' unless A133Recovery.respond_to?(:entry_plan)
checks=0
[false,true].product(["\0".b,"\xff".b]).each do |leading,pad|
  original=encode.call(vars,leading,pad)
  retained=original.dup
  plan=A133Recovery.entry_plan(original)
  expected=encode.call(vars.merge(helpers).merge('bootcmd'=>armed_cmd),leading,pad)
  abort 'FAIL: direct armed bytes, input or frozen result' unless plan[:armed_env]==expected &&
    original==retained && plan.frozen? && plan[:armed_env].frozen? && plan[:consumed].frozen? &&
    plan[:profile]=='direct' && plan[:installation_ready]==false
  abort 'FAIL: normal boot was not restored before recovery' unless plan[:consumed]==vars.merge(helpers)
  restored=A133Env.patch(plan[:armed_env],helpers.transform_values { '' }.merge('bootcmd'=>vars['bootcmd']))
  abort 'FAIL: original byte restoration' unless restored==original
  checks+=1
end
reject=lambda do |entries,reason='unsupported_factory_environment'|
  begin
    A133Recovery.entry_plan(encode.call(entries,false,"\xff".b))
    abort 'FAIL: unsafe direct factory profile accepted'
  rescue A133Recovery::Invalid => error
    abort 'FAIL: direct refusal reason or private cause' unless error.message==reason && error.cause.nil?
  end
  checks+=1
end
%w[bootcmd boot_normal boot_recovery].each { |key| reject.call(vars.merge(key=>'run foreign')) }
reject.call(vars.reject { |key,_| key=='setargs_nand' })
reject.call(vars.merge('setargs_nand'=>''))
(helpers.keys+%w[hook boot_android ember_restore_normal ember_recovery_once]).each do |key|
  ['', 'foreign'].each { |value| reject.call(vars.merge(key=>value),'recovery_hook_already_present') }
end
original=encode.call(vars,false,"\xff".b)
original.setbyte(0,original.getbyte(0)^1)
begin
  A133Recovery.entry_plan(original); abort 'FAIL: corrupt direct CRC accepted'
rescue A133Recovery::Invalid => error
  abort 'FAIL: corrupt direct reason' unless error.message=='invalid_environment' && error.cause.nil?
end
checks+=1
puts "Direct recovery plan: #{checks} byte preservation and refusal cases passed"
