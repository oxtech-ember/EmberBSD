#!/usr/bin/env ruby
# Origin: EmberBSD - real-file image journal scope, publication and release admission regressions.
require 'open3'
require 'rbconfig'
tool=File.join(__dir__,'a133-install-session.rb')
child=<<~RUBY
  require 'tmpdir'
  require #{tool.inspect}
  context={'schema'=>1,'board'=>'ys-m33-a133','serial'=>'SESSION_TEST','cid'=>'0'*32,
    'source_commit'=>'a'*40,'backup_sha256'=>'b'*64,'recovery_sha256'=>'c'*64,
    'protected_env_sha256'=>'d'*64,'artifacts'=>%w[root boot resources].map do |role|
      {'role'=>role,'bytes'=>role=='root' ? 1024 : 33554432,'sha256'=>'e'*64}
    end}
  Dir.mktmpdir('a133-session-regression-') do |base|
    dir=File.join(base,'session')
    A133Install::Store.open(dir,context) do |first|
      if ARGV.first=='fsync'
        module ImageSessionFsyncFault
          class << self; attr_accessor :active; end
          def fsync
            value=super
            if ImageSessionFsyncFault.active && stat.directory?
              ImageSessionFsyncFault.active=false
              raise Errno::EIO,'PRIVATE_FSYNC'
            end
            value
          end
        end
        File.prepend(ImageSessionFsyncFault)
        ImageSessionFsyncFault.active=true
        begin
          first.record('root','writing'); abort 'FAIL: ignored fsync failure'
        rescue A133Install::Invalid => error
          abort 'FAIL: fsync reason' unless error.message=='session_io_failed'
        end
        abort 'FAIL: visible writing intent lost in report' unless first.report[:roles]['root']['state']=='writing'
        first.record('boot','failed','session_io_failed')
        abort 'FAIL: failure recording erased writing intent' unless
          JSON.parse(File.read(File.join(dir,'state.json')))['roles']['root']['state']=='writing'
      else
        if ARGV.first=='directory'
          File.rename(dir,dir+'.moved')
        elsif ARGV.first=='lock'
          File.rename(File.join(dir,'.lock'),File.join(dir,'.lock.moved'))
        elsif ARGV.first=='permissions'
          File.chmod(0755,dir)
        else
          abort 'FAIL: unknown case'
        end
        attempt=lambda do
          begin
            first.record('root','writing'); abort 'FAIL: detached lock accepted writing'
          rescue A133Install::Invalid
          end
        end
        if ARGV.first=='permissions'
          before=File.binread(File.join(dir,'state.json')); attempt.call
          abort 'FAIL: unsafe directory changed bytes' unless File.binread(File.join(dir,'state.json'))==before
          File.chmod(0700,dir)
        else
          A133Install::Store.open(dir,context) do |second|
            before=File.binread(File.join(dir,'state.json')); attempt.call
            abort 'FAIL: replacement overwritten' unless File.binread(File.join(dir,'state.json'))==before
          end
        end
      end
    end
  end
RUBY
failures=[]
%w[fsync directory lock permissions].each do |name|
  out,err,status=Open3.capture3(RbConfig.ruby,'-e',child,name)
  failures << "#{name}: #{out}#{err}" unless status.success? && out.empty? && err.empty?
end
require 'tmpdir'
require tool
require_relative 'a133-usb-test-support'
Dir.mktmpdir('a133-bundle-admission-') do |dir|
  UsbFixture.create(dir); UsbFixture.state(dir)
  artifacts=%w[root boot resources].map do |role|
    file=role=='root' ? 'source' : role+'.bin'
    File.open(File.join(dir,file),'wb') { |f| f.truncate(33554432) } unless role=='root'
    {'role'=>role,'file'=>file,'bytes'=>File.size(File.join(dir,file)),
      'sha256'=>Digest::SHA256.file(File.join(dir,file)).hexdigest}
  end
  manifest=File.join(dir,'bundle.json')
  File.write(manifest,JSON.generate({'schema'=>1,'board'=>'ys-m33-a133','source_commit'=>'a'*40,'artifacts'=>artifacts}))
  begin
    A133Install.run(directory:File.join(dir,'session'),manifest:manifest,adb:File.join(dir,'adb'),
      serial:'USB_SAMPLE',cid:UsbFixture::CID,backup:UsbFixture.backup(dir),protected_env:UsbFixture.env,
      expected_bundle_sha256:'0'*64,timeout:5)
    failures << 'bundle: changed expected release accepted'
  rescue A133Install::Invalid => error
    failures << 'bundle: wrong bounded reason '+error.message unless error.message=='session_bundle_changed'
  rescue ArgumentError
    failures << 'bundle: expected release admission is missing'
  end
  failures << 'bundle: admission touched USB or created journal' if File.exist?(File.join(dir,'observed')) ||
    File.exist?(File.join(dir,'session')) || File.exist?(File.join(dir,'written'))
end
abort failures.join("\n") unless failures.empty?
puts 'Image session regressions: 5 publication, lock scope and release admission cases passed'
