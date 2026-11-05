#!/usr/bin/env ruby
# Origin: EmberBSD - durable trial intent in the existing cable journal.
require 'tmpdir'
require_relative 'a133-cable-journal'
abort 'FAIL: trial journal API is missing' unless A133Cable::Journal.method_defined?(:record_trial)

checks=0
reject=lambda do |reason,&work|
  begin
    work.call
    abort 'FAIL: trial journal accepted an invalid transition'
  rescue A133Cable::Invalid=>error
    abort 'FAIL: incorrect trial refusal' unless error.message==reason && error.cause.nil?
  end
  checks+=1
end
context={'serial'=>'TRIAL_JOURNAL_TEST','cid'=>'0'*32}
A133Cable::Journal::HASHES.each { |key| context[key]='a'*64 }
trial={'phase'=>'arming','host_key_sha256'=>'1'*64,'kernel_version_sha256'=>'2'*64,
  'nonce_sha256'=>'3'*64,'boot_sha256'=>'4'*64,'recovery_epoch_sha256'=>'5'*64,
  'protected_env_sha256'=>'a'*64,'trial_env_sha256'=>'6'*64,'arm_helper_sha256'=>'7'*64}
native={'phase'=>'confirming','host_key_sha256'=>'1'*64,'kernel_version_sha256'=>'2'*64,
  'nonce_sha256'=>'3'*64,'boot_sha256'=>'4'*64,'epoch_sha256'=>'8'*64,
  'before_env_sha256'=>'9'*64,'normal_env_sha256'=>'b'*64}

Dir.mktmpdir('a133-trial-journal-') do |base|
  directory=File.join(base,'session')
  A133Cable::Journal.open(directory,context) do |store|
    reject.call('cable_trial_not_verified') { store.record_trial(trial) }
    store.record('unlocked',write:true,reboot:true,unlocked:true,hardware_bytes:4194304)
    store.record('protecting',protection:true)
    store.record('verified')
    reject.call('cable_trial_transition_invalid') { store.record_trial(trial.merge('phase'=>'armed')) }
    reject.call('cable_trial_state_invalid') { store.record_trial(trial.merge('extra'=>'private')) }
    reject.call('cable_trial_context_changed') { store.record_trial(trial.merge('protected_env_sha256'=>'c'*64)) }
    store.record_trial(trial)
    trial['nonce_sha256']='d'*64
    saved=store.snapshot.fetch('trial')
    abort 'FAIL: trial binding aliases caller' unless saved['nonce_sha256']=='3'*64
    reject.call('cable_trial_context_changed') { store.record_trial(trial) }
    reject.call('cable_trial_transition_invalid') { store.record_trial(saved.merge('phase'=>'mcu_armed')) }
    reject.call('cable_trial_transition_invalid') { store.record('writing') }
    store.record_trial(saved.merge('phase'=>'armed'))
    reject.call('cable_trial_transition_invalid') { store.record_trial(saved) }
    checks+=1
  end
  A133Cable::Journal.open(directory,nil) do |store|
    saved=store.snapshot.fetch('trial')
    abort 'FAIL: trial intent was not durable' unless saved['phase']=='armed'
    %w[mcu_arming mcu_armed rebooting rebooted].each do |phase|
      store.record_trial(saved.merge('phase'=>phase))
    end
    store.record_trial(saved.merge('phase'=>'rebooted'))
    reject.call('cable_native_context_changed') do
      store.record_native(native.merge('nonce_sha256'=>'c'*64))
    end
    store.record_native(native)
    reject.call('cable_trial_transition_invalid') { store.record_trial(saved.merge('phase'=>'rebooted')) }
    abort 'FAIL: trial intent claimed installation readiness' unless store.report[:installation_ready]==false
    checks+=1
  end
  # Journals from before the trial API remain valid, including native state.
  file=File.join(directory,'state.json')
  data=JSON.parse(File.read(file));data.delete('trial')
  data['checksum']=A133Cable::Journal.digest(data.reject { |key,_| key=='checksum' })
  File.write(file,JSON.generate(data))
  A133Cable::Journal.open(directory,nil) do |store|
    abort 'FAIL: legacy journal was changed' if store.snapshot.key?('trial')
    reject.call('cable_trial_transition_invalid') { store.record_trial(trial) }
  end
  checks+=1
end
puts "Trial cable journal: #{checks} persistence, binding and transition cases passed"
