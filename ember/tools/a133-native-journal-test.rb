#!/usr/bin/env ruby
# Origin: EmberBSD - native confirmation intent in the existing cable journal.
require 'tmpdir'
require_relative 'a133-cable-journal'
abort 'FAIL: native journal API is missing' unless A133Cable::Journal.method_defined?(:record_native)
checks=0
failure=lambda do |reason,&block|
  begin
    block.call;abort 'FAIL: native journal accepted invalid input'
  rescue A133Cable::Invalid=>error
    abort 'FAIL: wrong native refusal' unless error.message==reason && error.cause.nil?
  end
  checks+=1
end
context={'serial'=>'NATIVE_JOURNAL_TEST','cid'=>'0'*32}
A133Cable::Journal::HASHES.each { |key| context[key]='a'*64 }
native={'phase'=>'confirming','host_key_sha256'=>'b'*64,'kernel_version_sha256'=>'0'*64,'nonce_sha256'=>'c'*64,
  'boot_sha256'=>'d'*64,'epoch_sha256'=>'e'*64,'before_env_sha256'=>'f'*64,'normal_env_sha256'=>'1'*64}
Dir.mktmpdir('a133-native-journal-') do |base|
  dir=File.join(base,'session')
  A133Cable::Journal.open(dir,context) do |store|
    failure.call('cable_native_not_verified') { store.record_native(native) }
    store.record('unlocked',write:true,reboot:true,unlocked:true,hardware_bytes:4194304)
    store.record('protecting',protection:true)
    store.record('verified')
    store.record_native(native)
    native['nonce_sha256']='9'*64
    saved=store.snapshot.fetch('native')
    abort 'FAIL: native context aliases caller' unless saved['nonce_sha256']=='c'*64
    abort 'FAIL: confirmation lost marker write intent' unless store.report[:possible_write] && store.report[:installation_ready]==false
    failure.call('cable_native_context_changed') { store.record_native(native) }
    native=saved
    failure.call('cable_native_transition_invalid') { store.record_native(native.merge('phase'=>'released')) }
    failure.call('cable_native_state_invalid') { store.record_native(native.merge('phase'=>'unknown')) }
    failure.call('cable_native_state_invalid') { store.record_native(native.merge('private'=>'secret')) }
    failure.call('cable_native_state_invalid') { store.record_native(native.merge('epoch_sha256'=>nil)) }
    store.record_native(native.merge('phase'=>'confirmed'))
    failure.call('cable_native_transition_invalid') { store.record_native(native) }
    failure.call('cable_native_transition_invalid') { store.record('writing') }
    checks+=1
  end
  A133Cable::Journal.open(dir,nil) do |store|
    current=store.snapshot.fetch('native')
    abort 'FAIL: native intent did not persist' unless current['phase']=='confirmed'
    store.record_native(current.merge('phase'=>'releasing'))
    store.record_native(current.merge('phase'=>'released'))
    store.record_native(current.merge('phase'=>'released'))
    abort 'FAIL: released journal changed reboot flag' unless store.report[:possible_reboot]
    checks+=1
  end
  # Old cable journals remain readable. Unknown fields still fail closed.
  bytes=File.binread(File.join(dir,'state.json'))
  parsed=JSON.parse(bytes)
  parsed.delete('native')
  parsed['checksum']=A133Cable::Journal.digest(parsed.reject { |key,_| key=='checksum' })
  File.binwrite(File.join(dir,'state.json'),JSON.generate(parsed))
  A133Cable::Journal.open(dir,nil) { |store| abort 'FAIL: old journal migration' if store.snapshot.key?('native') }
  checks+=1
end
puts "Native cable journal: #{checks} persistence and transition cases passed"
