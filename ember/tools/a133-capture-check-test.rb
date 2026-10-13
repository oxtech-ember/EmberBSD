#!/usr/bin/env ruby
# Origin: EmberBSD - private capture policy and actual image/copy revalidation.
require 'open3'
require 'rbconfig'
require 'tmpdir'
require_relative 'a133-capture-test-support'
tool = File.join(__dir__,'a133-capture-check.rb')
abort 'FAIL: capture verifier is missing' unless File.file?(tool)
require tool
CaptureFixture.configure
# Faults at real File#read keep image/copy verification and bytes intact.
module CaptureReadFault
  def read(*arguments)
    value = super
    fault = $capture_read_fault
    if fault && File.basename(path)=='boot.bin'
      $capture_read_fault = nil
      if fault[:kind]==:mutate_backup
        File.open(fault[:target],'ab') { |file| file.write('x') }
      else
        sleep 2
      end
    end
    value
  end
end
File.prepend(CaptureReadFault)
checks = 0
serial = 'CAPTURE_TEST_SERIAL'
cid = '0123456789abcdef0123456789abcdef'
Dir.mktmpdir('capture-check-') do |dir|
  manifest = CaptureFixture.write(dir)
  path = File.join(dir,'capture.json')
  run = ->(**args) { A133Capture.verify(path, serial:serial, cid:cid, **args) }
  receipt = run.call
  abort 'FAIL: bound receipt lost real evidence' unless receipt['status']=='backup_integrity_verified' &&
    receipt['bytes']==262144 && receipt['cid']==cid && receipt['serial']==serial &&
    receipt['partition_sha256']=={'bootloader'=>Digest::SHA256.hexdigest('a'*8192),
      'env'=>Digest::SHA256.hexdigest('b'*8192),'boot'=>Digest::SHA256.hexdigest('c'*8192),
      'recovery'=>Digest::SHA256.hexdigest('d'*8192)} && receipt['writes_performed']==0 &&
    receipt['installation_ready']==false && receipt['critical_copies_verified']==true &&
    receipt['filesystem_consistency']=='not_established_by_integrity_check'
  checks += 1
  reject = lambda do |reason,&block|
    begin
      block.call
      abort "FAIL: accepted #{reason}"
    rescue A133Capture::Invalid => error
      abort "FAIL: wanted #{reason}, got #{error.message}" unless error.message==reason
      abort 'FAIL: private error leak' if error.message.include?(dir) || error.message.include?(cid)
    end
    checks += 1
  end
  reject.call('capture_identity_mismatch') { A133Capture.verify(path,serial:'OTHER',cid:cid) }
  reject.call('capture_identity_mismatch') { A133Capture.verify(path,serial:serial,cid:'f'*32) }
  reject.call('invalid_capture_identity') { A133Capture.verify(path,serial:serial,cid:nil) }
  changed = Marshal.load(Marshal.dump(manifest)); changed.delete('cid')
  File.write(path,JSON.generate(changed))
  reject.call('invalid_capture_fields') { run.call }
  CaptureFixture.write(dir)
  File.binwrite(File.join(dir,'boot.bin'),'x'*8192)
  reject.call('critical_copy_hash_mismatch') { run.call }
  CaptureFixture.write(dir)
  File.binwrite(File.join(dir,'env.bin'),'b'*8191)
  reject.call('critical_copy_size_mismatch') { run.call }
  CaptureFixture.write(dir)
  File.open(File.join(dir,'disk.raw'),'r+b') { |f| f.seek(100000); f.write('x') }
  reject.call('disk_sha256_mismatch') { run.call }
  CaptureFixture.write(dir)
  File.chmod(0755,dir)
  reject.call('unsafe_capture_directory') { run.call }
  File.chmod(0700,dir)
  %w[capture.json disk.raw boot.bin].each do |name|
    file = File.join(dir,name)
    File.chmod(0644,file)
    reject.call('unsafe_capture_file') { run.call }
    File.chmod(0600,file)
    File.link(file,File.join(dir,'alias'))
    reject.call('unsafe_capture_file') { run.call }
    File.unlink(File.join(dir,'alias'))
    File.rename(file,File.join(dir,'original'))
    File.symlink(File.join(dir,'original'),file)
    reject.call('unsafe_capture_file') { run.call }
    File.unlink(file); File.rename(File.join(dir,'original'),file)
  end
  changes = [
    ['unsupported_capture',->(m) {m['board']='another-a133'}],
    ['unsupported_capture',->(m) {m['schema']=1.0}],
    ['invalid_capture_fields',->(m) {m['receipt']={'status'=>'backup_integrity_verified'}}],
    ['invalid_capture_backup',->(m) {m['backup']['format']='gzip'}],
    ['invalid_capture_filename',->(m) {m['backup']['file']='../outside'}],
    ['duplicate_capture_filename',->(m) {m['partitions'][0]['file']='disk.raw'}],
    ['invalid_capture_partitions',->(m) {m['partitions'].pop}],
    ['invalid_capture_partitions',->(m) {m['partitions'][0]['role']='boot'}],
    ['invalid_capture_sha256',->(m) {m['uncompressed_sha256']='0'*63}]
  ]
  changes.each do |reason,change|
    changed = Marshal.load(Marshal.dump(manifest)); change.call(changed)
    File.write(path,JSON.generate(changed))
    reject.call(reason) { run.call }
  end
  File.write(path,'{"schema":1,"schema":1}')
  reject.call('duplicate_capture_key') { run.call }
  File.write(path,'')
  reject.call('invalid_capture_json') { run.call }
  File.write(path,' '*65537)
  reject.call('capture_manifest_too_large') { run.call }
  CaptureFixture.write(dir)
  # Re-entry must freshly read both the image and independent copies.
  run.call
  File.binwrite(File.join(dir,'recovery.bin'),'x'*8192)
  reject.call('critical_copy_hash_mismatch') { run.call }
  CaptureFixture.write(dir)
  decoder = File.join(dir,'decoder')
  File.write(decoder,<<~'SCRIPT')
    #!/usr/bin/env ruby
    data = STDIN.read
    File.open(ENV.fetch('CAPTURE_MUTATE'),'ab') { |f| f.write('x') } if ENV['CAPTURE_MUTATE']
    STDOUT.write(data)
  SCRIPT
  File.chmod(0700,decoder)
  changed = Marshal.load(Marshal.dump(manifest)); changed['backup']['format']='zstd'
  File.write(path,JSON.generate(changed))
  receipt = run.call(zstd:decoder)
  abort 'FAIL: decoded capture did not verify' unless receipt['critical_copies_verified']==true
  checks += 1
  ENV['CAPTURE_MUTATE']=File.join(dir,'env.bin')
  reject.call('critical_copy_size_mismatch') { run.call(zstd:decoder) }
  ENV.delete('CAPTURE_MUTATE')
  CaptureFixture.write(dir)
  $capture_read_fault = {kind: :mutate_backup,target:File.join(dir,'disk.raw')}
  reject.call('capture_file_changed') { run.call }
  CaptureFixture.write(dir)
  $capture_read_fault = {kind: :deadline}
  reject.call('capture_read_timeout') { run.call(timeout:1) }
  $capture_read_fault = nil
  CaptureFixture.write(dir)
  bootstrap = File.join(dir,'cli-bootstrap.rb')
  File.write(bootstrap, "require #{File.join(__dir__,'a133-capture-test-support.rb').inspect}\nCaptureFixture.configure\n$0=#{tool.inspect}\nload #{tool.inspect}\n")
  out,err,status = Open3.capture3(RbConfig.ruby,bootstrap,'--serial',serial,'--cid',cid,path)
  public_receipt = JSON.parse(out)
  abort 'FAIL: CLI did not redact identity/evidence' unless status.success? && err.empty? &&
    public_receipt['status']=='capture_integrity_verified' && public_receipt['writes_performed']==0 &&
    public_receipt['installation_ready']==false && !out.include?(cid) && !out.include?(serial) &&
    !out.include?(dir) && !out.include?(manifest['uncompressed_sha256'])
  checks += 1
  out,err,status = Open3.capture3(RbConfig.ruby,tool,'--write',path)
  abort 'FAIL: capture CLI accepted write' unless !status.success? && err.empty? &&
    JSON.parse(out)['reason']=='invalid_arguments' && !out.include?(dir)
  checks += 1
end
puts "Capture binding: #{checks} private-file and fresh-read cases passed"
