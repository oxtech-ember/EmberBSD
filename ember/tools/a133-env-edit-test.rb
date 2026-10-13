#!/usr/bin/env ruby
# Origin: EmberBSD - test real vendor environment editing before storage writes.
require 'zlib'
require 'tmpdir'
require 'open3'
require 'rbconfig'
tool = File.expand_path('a133-env-edit.rb', __dir__)
abort 'FAIL: checked offline A133 environment editor is missing' unless File.file?(tool)
require tool
def assert(value)
  abort 'environment contract failed' unless value
end
def rejected
  begin
    yield
  rescue A133Env::Invalid
    return
  end
  abort 'invalid environment accepted'
end
def fixture(text)
  body = text.b.ljust(131068, "\0")
  [Zlib.crc32(body)].pack('V') + body
end
%w[leading ordinary].each do |which|
  lead = which == 'leading' ? "\0" : ''
  original = fixture(lead + "bootcmd=run boot_normal\0bootdelay=3\0opaque=a=b\0\0")
  edited = A133Env.patch(original, {'bootcmd' => 'run boot_recovery'})
  assert(original == fixture(lead + "bootcmd=run boot_normal\0bootdelay=3\0opaque=a=b\0\0"))
  decoded = A133Env.decode(edited)
  assert(decoded[:entries].to_h == {'bootcmd' => 'run boot_recovery', 'bootdelay' => '3', 'opaque' => 'a=b'})
  assert(decoded[:leading_empty] == (which == 'leading'))
  assert(A133Env.patch(edited, {'bootcmd' => 'run boot_normal'}) == original)
  removed = A133Env.decode(A133Env.patch(original, {'bootdelay' => ''}))
  assert(!removed[:entries].to_h.key?('bootdelay'))
  corrupt = original.dup; corrupt.setbyte(400, 1)
  rejected { A133Env.decode(corrupt) }
  rejected { A133Env.decode(original + "\0") }
  rejected { A133Env.patch(original, {'bootcmd' => "x\0y"}) }
  rejected { A133Env.patch(original, {'bootcmd' => 'x' * 131068}) }
end
rejected { A133Env.decode(fixture("bootcmd=a\0bootcmd=b\0\0")) }
rejected { A133Env.decode(fixture("bad_entry\0\0")) }
rejected { A133Env.decode(fixture('x' * 131068)) }
Dir.mktmpdir('a133-env-') do |dir|
  source = File.join(dir, 'source')
  output = File.join(dir, 'prepared')
  original = fixture("bootcmd=run boot_normal\0\0")
  File.binwrite(source, original)
  _, _, status = Open3.capture3(RbConfig.ruby, tool, source, output, 'bootcmd=run boot_recovery')
  assert(status.success? && (File.stat(output).mode & 0777) == 0600)
  assert(A133Env.decode(File.binread(output))[:entries].to_h['bootcmd'] == 'run boot_recovery')
  _, _, status = Open3.capture3(RbConfig.ruby, tool, source, source, 'bootcmd=overwrite')
  assert(!status.success? && File.binread(source) == original)
  before = File.binread(output)
  _, _, status = Open3.capture3(RbConfig.ruby, tool, source, output, 'bootcmd=overwrite')
  assert(!status.success? && File.binread(output) == before)
end
puts 'Environment contract: CRC, bounded input, opaque values, leading entry, round-trip, duplicates and overflow passed'
