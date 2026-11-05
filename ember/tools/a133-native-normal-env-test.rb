#!/usr/bin/env ruby
# Origin: EmberBSD - native normal-boot byte transitions for both factory profiles.
# SPDX-License-Identifier: BSD-2-Clause
require 'zlib'
require_relative 'a133-recovery-policy'
abort 'FAIL: native normal-boot transition is missing' unless A133Recovery::Policy.method_defined?(:normal_boot_env)

checks=0
tail=("\xa5".b*(16777216-A133Env::SIZE)).freeze
encode=lambda do |entries,pad,leading|
  body=(leading ? "\0".b : ''.b)+entries.map { |k,v| k.b+'='+v.b+"\0" }.join+"\0"
  body=body.ljust(A133Env::SIZE-4,pad.chr)
  [Zlib.crc32(body)].pack('V')+body
end
backup=lambda do |original|
  {'serial'=>'NATIVE_POLICY_TEST','cid'=>'1'*32,'status'=>'backup_integrity_verified',
   'bytes'=>A133Backup::BYTES,'uncompressed_sha256'=>'a'*64,'gpt_sha256'=>'b'*64,
   'gpt_headers_crc'=>true,'gpt_arrays_crc'=>true,'partition_layout_verified'=>true,
   'critical_copies_verified'=>true,'hardware_boot_copies_verified'=>true,
   'partition_sha256'=>{'boot'=>'c'*64,'bootloader'=>'d'*64,'recovery'=>'e'*64,
      'env'=>Digest::SHA256.hexdigest(original)}}
end
reject=lambda do |policy,input,reason|
  begin
    policy.normal_boot_env(input)
    abort 'FAIL: unsafe normal-boot transition accepted'
  rescue A133Recovery::Invalid=>error
    abort 'FAIL: wrong normal-boot refusal or nested context' unless error.message==reason && error.cause.nil?
  end
  checks+=1
end
[false,true].product([0,255],[false,true]).each do |direct,pad,leading|
  factory=(direct ? A133Recovery::DIRECT_PROFILE : A133Recovery::PROFILE).dup
  factory['setargs_nand']=A133Recovery::DIRECT_SETARGS if direct
  factory.merge!('opaque'=>"\xff;keep=this".b,'empty'=>'')
  factory.merge!('mac'=>'','wifi_mac'=>'','bt_mac'=>'','specialstr'=>'') if direct
  original=encode.call(factory,pad,leading)+tail
  policy=A133Recovery::Policy.new(serial:'NATIVE_POLICY_TEST',cid:'1'*32,
    original_env:original,backup:backup.call(original))
  helpers=direct ? {
    'ember_entry_fdt'=>'fdt addr ${fdtcontroladdr}',
    'ember_entry_usb'=>'fdt set /soc@03000000/usbc0@0 usb_port_type <0>',
    'ember_recovery_once'=>'run ember_entry_fdt ember_entry_usb boot_recovery',
    'ember_trial_rearm'=>'setenv bootcmd run setargs_nand ember_recovery_once',
    'ember_trial_save'=>'saveenv',
    'ember_trial_load'=>'sunxi_flash read 45000000 boot',
    'ember_trial_boot'=>'bootm 45000000'
  } : {
    'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery',
    'ember_trial_rearm'=>'setenv boot_normal run ember_recovery_once',
    'ember_trial_save'=>'saveenv'
  }
  key=direct ? 'bootcmd' : 'boot_normal'
  protected_value=direct ? 'run setargs_nand ember_recovery_once' : 'run ember_recovery_once'
  values=factory.merge(key=>protected_value).merge(helpers)
  current=encode.call(values,pad,leading)+tail
  expected=encode.call(values.merge(key=>factory.fetch(key)),pad,leading)+tail
  before=current.dup
  result=policy.normal_boot_env(current)
  abort 'FAIL: wrong bytes, tail, immutability or changed input' unless result==expected && result.frozen? && current==before
  current.replace('changed caller input')
  abort 'FAIL: result aliases caller input' unless result==expected
  checks+=1
  reject.call(policy,result,'native_environment_not_rearmed')
  reject.call(policy,original,'native_environment_not_rearmed')
  bad=before.dup;bad.setbyte(0,bad.getbyte(0)^1)
  reject.call(policy,bad,'invalid_native_environment')
  bad=before.dup;bad.setbyte(-1,bad.getbyte(-1)^1)
  reject.call(policy,bad,'native_environment_tail_changed')
  bad=encode.call(values.merge('foreign'=>'run unknown'),pad,leading)+tail
  reject.call(policy,bad,'native_environment_not_rearmed')
  [nil,false,{},'',before.byteslice(0,131072),before+"\0"].each do |input|
    reject.call(policy,input,'native_environment_size_mismatch')
  end
  if direct
    exported=values.reject { |k,_| %w[wifi_mac bt_mac specialstr].include?(k) }.merge(
      'mac'=>'02:12:34:56:78:90','snum'=>'0011223344556677889','bootreason'=>'unknow',
      'fdtcontroladdr'=>'7beb3280','fileaddr'=>'40000000','filesize'=>'2a338','rotpk_status'=>'0',
      'partitions'=>A133Backup::INVENTORY.map { |part| "#{part[:name]}@mmcblk0p#{part[:index]}" }.join(':'))
    current=encode.call(exported,pad,leading)+tail
    expected=encode.call(exported.merge('bootcmd'=>factory.fetch('bootcmd')),pad,leading)+tail
    abort 'FAIL: measured runtime export was lost or changed' unless policy.normal_boot_env(current)==expected
    checks+=1
    [['mac','03:12:34:56:78:90'],['snum','FOREIGN'],['fileaddr','50000000'],
      ['filesize','1'],['rotpk_status','1'],['wifi_mac','']].each do |name,value|
      reject.call(policy,encode.call(exported.merge(name=>value),pad,leading)+tail,'native_environment_not_rearmed')
    end
  end
end
puts "Native normal env: #{checks} byte-preservation, identity and refusal cases passed"
