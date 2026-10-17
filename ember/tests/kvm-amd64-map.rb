# SPDX-License-Identifier: BSD-2-Clause
# Run actual amd64 dump-offset functions with modeled kcore types and allocator state.
require 'tmpdir'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
source = File.read(root + '/lib/libkvm/kvm_x86_64.c')
start = source.index('struct p2o {') or abort 'Missing dump map structure'
finish = source.index("\n/*\n * Machine-dependent initialization", start) or abort 'Missing function boundary'
production = source[start...finish]
license = source[0...source.index('#include')]
code = license + File.read(__dir__ + '/kvm-amd64-map-fixture.c').sub('@PRODUCTION@', production)
Dir.mktmpdir('ember-kvm-map-') do |work|
  File.write(work + '/test.c', code)
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  # -Wno-sign-compare is already in libkvm's target command for its int/u64 loop.
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -Wno-sign-compare -fsanitize=address,undefined'))
  out, err, status = Open3.capture3(*cc, *flags, work + '/test.c', '-o', work + '/test')
  abort out + err unless status.success?
  unless flags.include?('--analyze')
    %w[success oom retry empty-oom high-oom].each do |mode|
      out, err, status = Open3.capture3(work + '/test', mode)
      abort out + err unless status.success?
      puts out
    end
  end
end
