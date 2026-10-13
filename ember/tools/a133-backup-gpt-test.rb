#!/usr/bin/env ruby
# Origin: EmberBSD - independently encoded GPT binding through fresh backup/capture receipts.
require 'tmpdir'
require 'stringio'
require_relative 'a133-capture-check'
require_relative 'a133-capture-test-support'
CaptureFixture.configure
checks=0
Dir.mktmpdir('a133-gpt-') do |dir|
  manifest=CaptureFixture.write(dir)
  data=File.binread(File.join(dir,'disk.raw'))
  expected=Digest::SHA256.hexdigest(data.byteslice(0,17408)+data.byteslice(-17408,17408))
  stream=A133Backup.verify_stream(StringIO.new(data),data.bytesize,Digest::SHA256.hexdigest(data),CaptureFixture::INVENTORY)
  abort 'FAIL: missing stream GPT binding' unless stream[:gpt_sha256]==expected
  checks+=1
  file=A133Backup.verify_file(File.join(dir,'disk.raw'),sha256:Digest::SHA256.hexdigest(data),format:'raw')
  abort 'FAIL: missing raw GPT binding' unless file[:gpt_sha256]==expected
  checks+=1
  receipt=A133Capture.verify(File.join(dir,'capture.json'),serial:manifest['serial'],cid:manifest['cid'])
  abort 'FAIL: missing capture GPT binding' unless receipt['gpt_sha256']==expected
  checks+=1
  [1,511].each do |lba|
    offset=lba*512
    data[offset+56,16]='G'*16
    data[offset+16,4]=[0].pack('V')
    data[offset+16,4]=[Zlib.crc32(data.byteslice(offset,92))].pack('V')
  end
  changed=A133Backup.verify_stream(StringIO.new(data),data.bytesize,Digest::SHA256.hexdigest(data),CaptureFixture::INVENTORY)
  abort 'FAIL: valid changed disk GUID was not bound' unless changed[:gpt_sha256]!=expected &&
    changed[:partition_sha256]==stream[:partition_sha256] && changed[:gpt_sha256]==Digest::SHA256.hexdigest(data.byteslice(0,17408)+data.byteslice(-17408,17408))
  checks+=1
end
puts "Backup GPT binding: #{checks} stream/raw/capture/changed-GUID cases passed"
