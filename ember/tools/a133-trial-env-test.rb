#!/usr/bin/env ruby
# Origin: EmberBSD - independently encoded one-shot trial and rearmed recovery byte contracts.
# SPDX-License-Identifier: BSD-2-Clause
require 'zlib'
require_relative 'a133-recovery-env'

abort 'FAIL: offline trial preparer is missing' unless A133Recovery.respond_to?(:trial)

# A wrong save/boot order, overwritten factory variable or accepted helper
# collision must change these independently constructed output bytes.
vars={
  'bootcmd'=>'run setargs_mmc boot_normal',
  'boot_normal'=>'run ${hook};run boot_android',
  'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
  'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000',
  'opaque'=>"\xff\xfe;keep=this".b, 'empty_value'=>'', 'bootdelay'=>'0'
}
encode=lambda do |entries,pad=0,leading=true|
  body=leading ? "\0".b : ''.b
  body << entries.map { |key,value| key.b+'='+value.b+"\0" }.join << "\0"
  raise 'fixture overflow' if body.bytesize>131068
  body=body.ljust(131068,pad.chr)
  [Zlib.crc32(body)].pack('V')+body
end
helpers={
  'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery',
  'ember_trial_rearm'=>'setenv boot_normal run ember_recovery_once',
  'ember_trial_save'=>'saveenv'
}
checks=0
[0,255].product([true,false]).each do |pad,leading|
  original=encode.call(vars,pad,leading)
  before=original.dup
  result=A133Recovery.trial(original)
  trial=encode.call(vars.merge('boot_normal'=>'run ember_trial_rearm ember_trial_save boot_android').merge(helpers),pad,leading)
  rearmed=encode.call(vars.merge('boot_normal'=>'run ember_recovery_once').merge(helpers),pad,leading)
  abort 'FAIL: trial bytes, metadata or input preservation' unless result.keys.sort==[:installation_ready,:rearmed_env,:trial_env] &&
    result[:trial_env]==trial && result[:rearmed_env]==rearmed && result[:installation_ready]==false && original==before
  abort 'FAIL: mutable prepared bytes' unless result.frozen? && result[:trial_env].frozen? && result[:rearmed_env].frozen?
  recovered=A133Env.patch(result[:rearmed_env],{'boot_normal'=>vars['boot_normal'],
    'ember_recovery_once'=>'','ember_trial_rearm'=>'','ember_trial_save'=>''})
  abort 'FAIL: trial is not exactly reversible' unless recovered==before
  original.replace('changed caller input')
  abort 'FAIL: caller owns prepared bytes' unless result[:trial_env]==trial && result[:rearmed_env]==rearmed
  checks+=1
end
reject=lambda do |input,reason|
  begin
    A133Recovery.trial(input)
    abort 'FAIL: unsafe trial input accepted'
  rescue A133Recovery::Invalid => error
    abort 'FAIL: wrong or unbounded trial refusal' unless error.message==reason && error.cause.nil?
  end
  checks+=1
end
%w[ember_trial_rearm ember_trial_save].each do |name|
  ['', 'run foreign'].each { |value| reject.call(encode.call(vars.merge(name=>value)),'trial_hook_already_present') }
end
%w[hook ember_restore_normal ember_recovery_once].each do |name|
  ['', 'run foreign'].each { |value| reject.call(encode.call(vars.merge(name=>value)),'recovery_hook_already_present') }
end
%w[bootcmd boot_normal boot_android boot_recovery].each do |name|
  reject.call(encode.call(vars.merge(name=>'run foreign')),'unsupported_factory_environment')
end
original=encode.call(vars)
bad=original.dup; bad.setbyte(0,bad.getbyte(0)^1)
duplicate=original.dup
body=duplicate.byteslice(4,131068)
body=body.sub("bootdelay=0\0\0","bootdelay=0\0bootdelay=1\0\0").byteslice(0,131068)
duplicate=[Zlib.crc32(body)].pack('V')+body
[nil,false,0,{},[], '',original[0...-1],original+"\0",bad,duplicate].each do |input|
  reject.call(input,'invalid_environment')
end
reject.call(encode.call(vars.merge('fill'=>'X'*130600)),'invalid_environment')
reject.call(A133Recovery.protect(original),'recovery_hook_already_present')
[['hook','recovery_hook_already_present'],['bootcmd','unsupported_factory_environment']].each do |name,reason|
  begin
    raise 'PRIVATE_CALLER_CONTEXT'
  rescue RuntimeError
    reject.call(encode.call(vars.merge(name=>'foreign')),reason)
  end
end
puts "Trial env: #{checks} byte-preservation, reversal, collision and refusal cases passed"
