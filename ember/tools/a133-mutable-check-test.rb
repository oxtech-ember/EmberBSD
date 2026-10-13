#!/usr/bin/env ruby
# Origin: EmberBSD - fresh real-file mutable hashes, identity, privacy and lifetime contracts.
require 'tmpdir'
require 'open3'
require 'rbconfig'
require_relative 'a133-mutable-test-support'
tool=File.join(__dir__,'a133-mutable-check.rb')
abort 'FAIL: mutable checker is missing' unless File.file?(tool)
require tool
MutableFixture.configure
module MutableLateFileFault
  def read(*args)
    value=super
    fault=$mutable_file_fault
    if fault && File.basename(path)=='metadata.raw'
      $mutable_file_fault=nil
      if fault==:directory
        File.chmod(0755,File.dirname(path))
      else
        udisk=File.join(File.dirname(path),'udisk.raw')
        time=File.stat(udisk).mtime
        File.binwrite(udisk,'X'*102912); File.utime(time,time,udisk)
      end
    end
    value
  end
end
File.prepend(MutableLateFileFault)
checks=0
Dir.mktmpdir('mutable-check-') do |parent|
  File.chmod(0700,parent)
  index=0
  fresh=lambda do
    index+=1; directory=File.join(parent,index.to_s)
    record=MutableFixture.write(directory)
    [directory,record,File.join(directory,'mutable.json')]
  end
  verify=lambda { |path,**options| A133Mutable.verify(path,serial:'MUTABLE_TEST_SERIAL',cid:'0123456789abcdef0123456789abcdef',**options) }
  reject=lambda do |path,reason,**options|
    begin
      verify.call(path,**options); abort 'FAIL: accepted '+reason
    rescue A133Mutable::Invalid => error
      abort 'FAIL: expected '+reason+', got '+error.message unless error.message==reason
      abort 'FAIL: private error cause/content' unless error.cause.nil? && !error.full_message.include?(parent) && !error.full_message.include?('PRIVATE_MUTABLE_MARKER')
      checks+=1
    end
  end
  directory,record,path=fresh.call
  result=verify.call(path)
  abort 'FAIL: fresh verification receipt' unless result['status']=='mutable_integrity_verified' &&
    result['mutable_copies_verified']==true && result['serial']=='MUTABLE_TEST_SERIAL' &&
    result['partition_sha256']==record['partitions'].to_h { |part| [part['role'],part['sha256']] } &&
    result['observation']=='recovery_unmounted_two_matching_reads' && result['installation_ready']==false &&
    result['filesystem_consistency']=='not_established_by_integrity_check'
  checks+=1
  File.binwrite(File.join(directory,'udisk.raw'),'X'*102912)
  reject.call(path,'mutable_copy_hash_mismatch')
  reject.call(path,'mutable_identity_mismatch',cid:'f'*32)
  mutations=[
    ['invalid_mutable_fields',->(r){r['extra']='PRIVATE_MUTABLE_MARKER'}],
    ['unsupported_mutable_capture',->(r){r['schema']=2}],
    ['invalid_mutable_source',->(r){r['capture_state']='device'}],
    ['invalid_mutable_source',->(r){r['observation']='consistent'}],
    ['invalid_mutable_sha256',->(r){r['gpt_sha256']='bad'}],
    ['invalid_mutable_partitions',->(r){r['partitions'][1]['role']='UDISK'}],
    ['invalid_mutable_partitions',->(r){r['partitions'][0]['bytes']=102911}],
    ['invalid_mutable_partitions',->(r){r['partitions'][0]['sha256']='A'*64}],
    ['invalid_mutable_filename',->(r){r['partitions'][0]['file']='../udisk.raw'}]
  ]
  mutations.each do |reason,mutate|
    directory,record,path=fresh.call; mutate.call(record)
    File.write(path,JSON.generate(record)); reject.call(path,reason)
  end
  directory,record,path=fresh.call
  File.write(path,JSON.generate(record).sub('"schema":1','"schema":1,"schema":1'))
  reject.call(path,'duplicate_mutable_key')
  directory,record,path=fresh.call
  File.write(path,'PRIVATE_MUTABLE_MARKER'); reject.call(path,'invalid_mutable_json')
  directory,record,path=fresh.call
  File.write(path,'x'*65537); reject.call(path,'mutable_manifest_too_large')
  directory,record,path=fresh.call
  File.chmod(0644,File.join(directory,'udisk.raw')); reject.call(path,'unsafe_mutable_file')
  directory,record,path=fresh.call
  File.truncate(File.join(directory,'udisk.raw'),102911); reject.call(path,'mutable_copy_size_mismatch')
  directory,record,path=fresh.call
  File.unlink(File.join(directory,'metadata.raw')); File.symlink('udisk.raw',File.join(directory,'metadata.raw'))
  reject.call(path,'unsafe_mutable_file')
  directory,record,path=fresh.call
  File.link(File.join(directory,'udisk.raw'),File.join(directory,'alias.raw')); reject.call(path,'unsafe_mutable_file')
  directory,record,path=fresh.call
  if File.exist?(File.join(directory,'UDISK.raw'))
    record['partitions'][1]['file']='UDISK.raw'; File.write(path,JSON.generate(record))
    reject.call(path,'duplicate_mutable_inode')
  end
  [:file,:directory].each do |fault|
    directory,record,path=fresh.call; $mutable_file_fault=fault
    reject.call(path,fault==:file ? 'mutable_file_changed' : 'unsafe_mutable_directory')
    abort 'FAIL: lifetime fault did not execute' if $mutable_file_fault
    File.chmod(0700,directory)
  end
  directory,record,path=fresh.call
  bootstrap=File.join(parent,'cli.rb')
  File.write(bootstrap,"require #{File.join(__dir__,'a133-mutable-test-support.rb').inspect}\nMutableFixture.configure\n$0=#{tool.inspect}\nload #{tool.inspect}\n")
  out,err,status=Open3.capture3(RbConfig.ruby,bootstrap,'--serial','MUTABLE_TEST_SERIAL','--cid','0123456789abcdef0123456789abcdef',path)
  abort 'FAIL: CLI leaked/failed' unless status.success? && err.empty? && JSON.parse(out)['status']=='mutable_integrity_verified' &&
    !out.include?('MUTABLE_TEST_SERIAL') && !out.include?('0123456789abcdef') && !out.include?(parent)
  checks+=1
  out,err,status=Open3.capture3(RbConfig.ruby,tool,'--write',path)
  abort 'FAIL: checker write flag' unless !status.success? && err.empty? && JSON.parse(out)['reason']=='invalid_arguments'
  checks+=1
end
puts "Mutable checker: #{checks} fresh-file, identity and lifetime cases passed"
