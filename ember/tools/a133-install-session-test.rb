#!/usr/bin/env ruby
# Origin: EmberBSD - actual private-state, crash, lock and file-backed resume checks.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require_relative 'a133-install-session'
require_relative 'a133-usb-test-support'

checks=0
Dir.mktmpdir('a133-session-') do |dir|
  UsbFixture.create(dir)
  UsbFixture.state(dir)
  session=File.join(dir,'session')
  artifacts=%w[root boot resources].zip([1024,33554432,33554432]).map do |role,bytes|
    path=role=='root' ? File.join(dir,'source') : File.join(dir,"#{role}.bin")
    unless role=='root'
      File.open(path,'wb') { |file| file.truncate(bytes); file.write(role) }
    end
    {'role'=>role,'file'=>File.basename(path),'bytes'=>bytes,'sha256'=>Digest::SHA256.file(path).hexdigest}
  end
  manifest=File.join(dir,'bundle.json')
  content={'schema'=>1,'board'=>'ys-m33-a133','source_commit'=>'a'*40,'artifacts'=>artifacts}
  File.write(manifest,JSON.generate(content))
  options={directory: session,manifest: manifest,adb: File.join(dir,'adb'),serial: 'USB_SAMPLE',
    cid: UsbFixture::CID,backup: UsbFixture.backup(dir),protected_env: UsbFixture.env,timeout: 5}
  original=File.binread(File.join(dir,'source'))
  root=File.join(dir,'part17')
  env=File.join(dir,'part2')
  result=A133Install.run(**options)
  abort 'FAIL: coordinated stage result' unless result[:status]=='write_stage_verified' &&
    result[:writes_performed]==3 && result[:installation_ready]==false &&
    result[:roles].values.all? { |row| row['state']=='verified' }
  abort 'FAIL: real root write/tail' unless File.open(root,'rb') { |file| file.read(1024) }==original &&
    File.open(root,'rb') { |file| file.seek(2048); file.read(14) }=='TAIL_PRESERVED'
  [ ['boot',3],['resources',1] ].each do |role,index|
    abort 'FAIL: real multi-role bytes' unless Digest::SHA256.file(File.join(dir,"part#{index}")).hexdigest==
      artifacts.find { |item| item['role']==role }['sha256']
  end
  state_path=File.join(session,'state.json')
  snapshot=JSON.parse(File.read(state_path))
  abort 'FAIL: state privacy' unless File.stat(session).mode&077==0 &&
    %w[state.json .lock].all? { |name| File.stat(File.join(session,name)).mode&0777==0600 }
  checks+=1
  before=[root,env,File.join(dir,'part3'),File.join(dir,'part1')].map { |path| File.stat(path).mtime }
  again=A133Install.run(**options)
  after=[root,env,File.join(dir,'part3'),File.join(dir,'part1')].map { |path| File.stat(path).mtime }
  abort 'FAIL: repeated stage rewrote verified ranges' unless again[:writes_performed]==0 && before==after &&
    again[:revision]>result[:revision]
  checks+=1
  File.open(root,'r+b') { |file| file.write('X') }
  repaired=A133Install.run(**options)
  abort 'FAIL: saved verified skipped mutated bytes' unless repaired[:writes_performed]==1 &&
    File.open(root,'rb') { |file| file.read(1024) }==original
  checks+=1
  reject=lambda do |reason,&operation|
    begin
      operation.call
      abort "FAIL: accepted #{reason}"
    rescue A133Install::Invalid=>error
      abort "FAIL: #{reason}: #{error.message}" unless error.message==reason &&
        !error.message.include?(dir) && !JSON.generate(error.report).include?(UsbFixture::CID)
    end
    checks+=1
  end
  File.open(root,'r+b') { |file| file.write('X') }
  UsbFixture.state(dir,{'mode'=>'partial'})
  File.open(root,'r+b') { |file| file.seek(700); file.write('Y') }
  boot_before=File.stat(File.join(dir,'part3')).mtime
  reject.call('usb_command_failed') { A133Install.run(**options) }
  pending=JSON.parse(File.read(state_path))
  abort 'FAIL: failed write progress/later role' unless pending['roles']['root']['state']=='failed' &&
    File.stat(File.join(dir,'part3')).mtime==boot_before &&
    File.open(env,'rb') { |file| file.read(131072) }==UsbFixture.env
  UsbFixture.state(dir)
  resumed=A133Install.run(**options)
  abort 'FAIL: partial resume not repaired' unless resumed[:writes_performed]==1 &&
    File.open(root,'rb') { |file| file.read(1024) }==original
  checks+=1
  leaked=nil
  A133Install::Store.open(session,nil) { |store| leaked=store }
  reject.call('session_not_locked') { leaked.record('root','writing') }
  # An abrupt process exit leaves the durable writing hint; next run reads real bytes.
  pid=fork do
    A133Install::Store.open(session,nil) { |store| store.record('root','writing'); exit! 91 }
  end
  Process.wait(pid)
  abort 'FAIL: crash hint not saved' unless JSON.parse(File.read(state_path))['roles']['root']['state']=='writing'
  abort 'FAIL: crash hint triggered blind rewrite' unless A133Install.run(**options)[:writes_performed]==0
  checks+=1
  # Real second process must fail its nonblocking lock before doing any stage.
  A133Install::Store.open(session,nil) do |_store|
    pid=fork do
      begin
        A133Install::Store.open(session,nil) { |_other| exit! 1 }
        exit! 2
      rescue A133Install::Invalid=>error
        exit!(error.message=='session_busy' ? 0 : 3)
      end
    end
    _,status=Process.wait2(pid)
    abort 'FAIL: lock contention' unless status.success?
  end
  checks+=1
  changed=JSON.parse(JSON.generate(content)); changed['source_commit']='b'*40
  File.write(manifest,JSON.generate(changed))
  reject.call('session_context_changed') { A133Install.run(**options) }
  File.write(manifest,JSON.generate(content))
  reject.call('session_context_changed') do
    A133Install.run(**options.merge(backup: options[:backup].merge('uncompressed_sha256'=>'f'*64)))
  end
  UsbFixture.state(dir,{'cid'=>'f'*32})
  reject.call('usb_identity_changed') { A133Install.run(**options) }
  UsbFixture.state(dir)
  File.chmod(0755,session)
  reject.call('session_permissions') { A133Install.run(**options) }
  File.chmod(0700,session)
  old=File.binread(state_path)
  [ '', '{"schema":1,"schema":1}', old.sub('"revision":', '"unknown_revision":') ].each do |bytes|
    File.binwrite(state_path,bytes)
    reject.call('session_state_invalid') { A133Install::Store.open(session,nil) { |_store| } }
  end
  File.binwrite(state_path,old)
  tampered=JSON.parse(old); tampered['roles']['root']['state']='pending'
  File.write(state_path,JSON.generate(tampered))
  reject.call('session_state_invalid') { A133Install::Store.open(session,nil) { |_store| } }
  File.binwrite(state_path,old)
  File.chmod(0644,state_path)
  reject.call('session_file_unsafe') { A133Install::Store.open(session,nil) { |_store| } }
  File.chmod(0600,state_path)
  File.link(state_path,File.join(dir,'hardlink'))
  reject.call('session_file_unsafe') { A133Install::Store.open(session,nil) { |_store| } }
  File.unlink(File.join(dir,'hardlink'))
  File.rename(state_path,File.join(session,'saved'))
  File.symlink(File.join(session,'saved'),state_path)
  reject.call('session_file_unsafe') { A133Install::Store.open(session,nil) { |_store| } }
  File.unlink(state_path); File.rename(File.join(session,'saved'),state_path)
  File.symlink(session,File.join(dir,'session-link'))
  reject.call('session_directory_unsafe') do
    A133Install::Store.open(File.join(dir,'session-link'),nil) { |_store| }
  end
  # Inject only fsync at the OS boundary; actual state and dd operations remain real.
  File.open(root,'r+b') { |file| file.write('X') }
  original_fsync=File.instance_method(:fsync)
  File.define_method(:fsync) { raise Errno::EIO }
  begin
    reject.call('session_io_failed') { A133Install.run(**options) }
    abort 'FAIL: prewrite persistence failure wrote device' unless File.open(root,'rb') { |file| file.read(1) }=='X'
  ensure
    File.define_method(:fsync,original_fsync)
  end
  File.define_method(:fsync) do
    if File.file?(path) && path.include?('.tmp')
      data=JSON.parse(File.read(path))
      raise Errno::EIO if data['roles']['root']['state']=='verified'
    end
    original_fsync.bind(self).call
  end
  begin
    reject.call('session_io_failed') { A133Install.run(**options) }
    abort 'FAIL: postwrite failure not retained' unless File.open(root,'rb') { |file| file.read(1024) }==original &&
      JSON.parse(File.read(state_path))['roles']['root']['state']!='verified'
  ensure
    File.define_method(:fsync,original_fsync)
  end
  abort 'FAIL: postwrite failure resume rewrote identical bytes' unless A133Install.run(**options)[:writes_performed]==0
  checks+=1
  out,err,status=Open3.capture3(RbConfig.ruby,File.join(__dir__,'a133-install-session.rb'),session)
  parsed=JSON.parse(out)
  abort 'FAIL: redacted status CLI' unless status.success? && err.empty? && parsed['installation_ready']==false &&
    !out.include?('USB_SAMPLE') && !out.include?(UsbFixture::CID) && !out.include?(dir)
  checks+=1
  out,err,status=Open3.capture3(RbConfig.ruby,File.join(__dir__,'a133-install-session.rb'),'--write')
  abort 'FAIL: write CLI' unless !status.success? && err.empty? && JSON.parse(out)['installation_ready']==false
  checks+=1
end
puts "Install session: #{checks} filesystem, crash and guarded resume cases passed"
