#!/usr/bin/env ruby
# Origin: EmberBSD - standalone expected release pin mutation and false admission regressions.
require 'tmpdir'
require_relative 'a133-install-session'
require_relative 'a133-usb-test-support'
checks=0
Dir.mktmpdir('a133-release-pin-') do |dir|
  UsbFixture.create(dir); UsbFixture.state(dir)
  artifacts=%w[root boot resources].map do |role|
    file=role=='root' ? 'source' : role+'.bin'
    File.open(File.join(dir,file),'wb') { |f| f.truncate(33554432) } unless role=='root'
    {'role'=>role,'file'=>file,'bytes'=>File.size(File.join(dir,file)),
      'sha256'=>Digest::SHA256.file(File.join(dir,file)).hexdigest}
  end
  manifest=File.join(dir,'bundle.json')
  File.write(manifest,JSON.generate({'schema'=>1,'board'=>'ys-m33-a133','source_commit'=>'a'*40,'artifacts'=>artifacts}))
  expected='0'*64
  A133Bundle.singleton_class.prepend(Module.new do
    define_method(:verify) do |path|
      result=super(path)
      expected.replace(A133Install.bundle_digest(result[:receipt]))
      result
    end
  end)
  failures=[]
  [expected,false].each_with_index do |pin,index|
    session=File.join(dir,'session'+index.to_s)
    begin
      A133Install.run(directory:session,manifest:manifest,adb:File.join(dir,'absent'),serial:'USB_SAMPLE',
        cid:UsbFixture::CID,backup:UsbFixture.backup(dir),protected_env:UsbFixture.env,expected_bundle_sha256:pin,timeout:1)
      failures << 'accepted expected pin'
    rescue A133Install::Invalid => error
      failures << 'wrong pin refusal '+error.message unless error.message=='session_bundle_changed'
    end
    failures << 'pin changed admitted a journal' if File.exist?(session)
    checks+=1
  end
  abort failures.join("\n") unless failures.empty?
end
puts "Bundle pin regressions: #{checks} mutable-string and false admission cases passed"
