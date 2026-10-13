#!/usr/bin/env ruby
# Origin: EmberBSD - independent persistent recovery env byte contracts.
require 'zlib'
require_relative 'a133-recovery-env'
require_relative 'a133-usb-transfer'
abort 'FAIL: persistent protection preparer is missing' unless A133Recovery.respond_to?(:protect)
vars={
  'bootcmd'=>'run setargs_mmc boot_normal','boot_normal'=>'run ${hook};run boot_android',
  'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000',
  'opaque'=>"\xff\xfe;keep=this".b,'empty_value'=>'','bootdelay'=>'0'
}
encode=lambda do |entries,pad=0,leading=true|
  body=leading ? "\0".b : ''.b
  body << entries.map { |key,value| key.b+'='+value.b+"\0" }.join << "\0"
  body=body.ljust(131068,pad.chr)
  [Zlib.crc32(body)].pack('V')+body
end
checks=0
[[0,true],[255,false]].each do |pad,leading|
  original=encode.call(vars,pad,leading)
  candidate=A133Recovery.protect(original)
  expected=encode.call(vars.merge('boot_normal'=>'run ember_recovery_once',
    'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'),pad,leading)
  abort 'FAIL: prefix or unrelated bytes changed' unless candidate==expected && original==encode.call(vars,pad,leading)
  abort 'FAIL: original restoration' unless A133Env.patch(candidate,
    {'boot_normal'=>'run ${hook};run boot_android','ember_recovery_once'=>''})==original
  client=A133Usb::Client.new(adb:'adb',serial:'TEST_SERIAL',cid:'0'*32)
  backup={'status'=>'backup_integrity_verified','cid'=>'0'*32,'bytes'=>31037849600,
    'gpt_headers_crc'=>true,'gpt_arrays_crc'=>true,'partition_layout_verified'=>true,
    'uncompressed_sha256'=>'a'*64,'partition_sha256'=>%w[bootloader env boot recovery].to_h { |role| [role,'b'*64] }}
  begin
    client.write_verified(role:'boot',path:File.join(__dir__,'absent-test-image'),bytes:33554432,
      sha256:'f'*64,backup:backup,protected_env:candidate)
    abort 'FAIL: absent image accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: protection incompatible with image writer' unless error.message=='image_unavailable'
  end
  checks+=1
end
%w[hook ember_restore_normal ember_recovery_once].each do |name|
  ['', 'run something'].each do |value|
    begin
      A133Recovery.protect(encode.call(vars.merge(name=>value)))
      abort 'FAIL: reserved hook accepted'
    rescue A133Recovery::Invalid => error
      abort 'FAIL: wrong collision failure' unless error.message=='recovery_hook_already_present'
    end
    checks+=1
  end
end
%w[bootcmd boot_normal boot_android boot_recovery].each do |name|
  begin
    A133Recovery.protect(encode.call(vars.merge(name=>'run custom')))
    abort 'FAIL: custom boot profile accepted'
  rescue A133Recovery::Invalid => error
    abort 'FAIL: wrong profile failure' unless error.message=='unsupported_factory_environment'
  end
  checks+=1
end
original=encode.call(vars)
bad=original.dup; bad.setbyte(0,bad.getbyte(0)^1)
[bad,original[0...-1]].each do |data|
  begin
    A133Recovery.protect(data)
    abort 'FAIL: damaged env accepted'
  rescue A133Recovery::Invalid => error
    abort 'FAIL: wrong env failure' unless error.message=='invalid_environment'
  end
  checks+=1
end
puts "Recovery protection bytes: #{checks} cases passed"
