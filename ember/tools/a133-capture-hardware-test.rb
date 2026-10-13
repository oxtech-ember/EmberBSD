#!/usr/bin/env ruby
# Origin: EmberBSD - schema2 real-file hardware boot evidence and schema1 compatibility.
require 'tmpdir'
require_relative 'a133-capture-check'
require_relative 'a133-capture-test-support'
CaptureFixture.configure
module HardwareLateMutation
  def read(*args)
    value = super
    if $hardware_mutation && File.basename(path)=='boot1.bin'
      target = $hardware_mutation; $hardware_mutation = nil
      File.open(target,'ab') { |file| file.write('x') }
    end
    value
  end
end
File.prepend(HardwareLateMutation)
checks = 0
Dir.mktmpdir('hardware-capture-') do |dir|
  path = File.join(dir,'capture.json')
  setup = lambda do
    manifest = CaptureFixture.write(dir)
    manifest.merge!('schema'=>2,'capture_state'=>'device','root_method'=>'vendor_su',
      'hardware_boot'=>[{'role'=>'boot0','file'=>'boot0.bin','bytes'=>1024,'sha256'=>Digest::SHA256.hexdigest('e'*1024)},
        {'role'=>'boot1','file'=>'boot1.bin','bytes'=>1024,'sha256'=>Digest::SHA256.hexdigest('f'*1024)}])
    File.binwrite(File.join(dir,'boot0.bin'),'e'*1024)
    File.binwrite(File.join(dir,'boot1.bin'),'f'*1024)
    File.chmod(0600,File.join(dir,'boot0.bin'),File.join(dir,'boot1.bin'))
    File.write(path,JSON.generate(manifest))
    manifest
  end
  run = -> {A133Capture.verify(path,serial:'CAPTURE_TEST_SERIAL',cid:'0123456789abcdef0123456789abcdef')}
  manifest = setup.call
  receipt = run.call
  abort 'FAIL: hardware receipt/legacy hashes lost' unless receipt['hardware_boot_copies_verified']==true &&
    receipt['partition_sha256'].keys.sort==%w[boot bootloader env recovery] &&
    receipt['writes_performed']==0 && receipt['installation_ready']==false
  checks += 1
  reject = lambda do |reason,&block|
    begin
      block.call
      abort "FAIL: accepted #{reason}"
    rescue A133Capture::Invalid => error
      abort "FAIL: wanted #{reason}, got #{error.message}" unless error.message==reason && error.cause.nil?
    end
    checks += 1
  end
  changes = [
    ['invalid_capture_hardware',->(m) {m['hardware_boot'].pop}],
    ['invalid_capture_hardware',->(m) {m['hardware_boot'][1]['role']='boot0'}],
    ['invalid_capture_hardware',->(m) {m['hardware_boot'][0]['bytes']=0}],
    ['invalid_capture_hardware',->(m) {m['hardware_boot'][0]['bytes']=1025}],
    ['invalid_capture_hardware',->(m) {m['hardware_boot'][0]['bytes']=33554944}],
    ['invalid_capture_hardware',->(m) {m['hardware_boot'][0]['bytes']=512}],
    ['invalid_capture_hardware',->(m) {m['hardware_boot'][0]['sha256']='0'*63}],
    ['invalid_capture_fields',->(m) {m.delete('root_method')}],
    ['invalid_capture_source',->(m) {m['capture_state']='bootloader'}],
    ['invalid_capture_source',->(m) {m['root_method']='restart_root'}],
    ['duplicate_capture_filename',->(m) {m['hardware_boot'][0]['file']='disk.raw'}]
  ]
  changes.each do |reason,change|
    changed = Marshal.load(Marshal.dump(manifest)); change.call(changed)
    File.write(path,JSON.generate(changed))
    reject.call(reason) {run.call}
  end
  setup.call
  File.binwrite(File.join(dir,'boot1.bin'),'f'*1023)
  reject.call('hardware_copy_size_mismatch') {run.call}
  setup.call
  File.binwrite(File.join(dir,'boot1.bin'),'x'*1024)
  reject.call('hardware_copy_hash_mismatch') {run.call}
  setup.call
  File.chmod(0644,File.join(dir,'boot0.bin'))
  reject.call('unsafe_capture_file') {run.call}
  setup.call
  $hardware_mutation = File.join(dir,'boot0.bin')
  reject.call('capture_file_changed') {run.call}
  $hardware_mutation = nil
  setup.call
  if File.exist?(File.join(dir,'BOOT0.bin'))
    changed = Marshal.load(Marshal.dump(manifest))
    changed['hardware_boot'][1].merge!('file'=>'BOOT0.bin','sha256'=>Digest::SHA256.hexdigest('e'*1024))
    File.write(path,JSON.generate(changed))
    reject.call('duplicate_capture_inode') {run.call}
  end
  CaptureFixture.write(dir)
  receipt = run.call
  abort 'FAIL: schema1 implies hardware evidence' unless receipt['hardware_boot_copies_verified']==false
  checks += 1
end
puts "Hardware capture: #{checks} schema and real-file cases passed"
