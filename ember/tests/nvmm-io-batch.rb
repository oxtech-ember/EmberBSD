# SPDX-License-Identifier: BSD-2-Clause
# Run the actual batch helper and public descriptor with modeled guest-memory I/O.
require 'tmpdir'
require 'open3'
require 'shellwords'
root = File.expand_path(ARGV[0] || '../..', __dir__)
source = File.read(root + '/lib/libnvmm/libnvmm_x86.c')
header = File.read(root + '/lib/libnvmm/nvmm.h')
helper = source[/static int\nassist_io_batch\(.*?^}/m] or abort 'Missing batch helper'
descriptor = header[/struct nvmm_io \{.*?^};/m] or abort 'Missing I/O descriptor'
constants = source.lines.grep(/^#define (?:MIN\(|NVMM_IO_BATCH_SIZE\s)/).join
code = source[0...source.index('#include')] + File.read(__dir__ + '/nvmm-io-batch-fixture.c')
{'IO_DESCRIPTOR' => descriptor, 'CONSTANTS' => constants, 'PRODUCTION' => helper}.each { |key, value| code.sub!('@' + key + '@', value) }
Dir.mktmpdir('ember-nvmm-batch-') do |work|
  File.write(work + '/test.c', code)
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined'))
  out, err, status = Open3.capture3(*cc, *flags, work + '/test.c', '-o', work + '/test')
  abort out + err unless status.success?
  unless flags.include?('--analyze')
    out, err, status = Open3.capture3(work + '/test')
    abort out + err unless status.success?
    puts out
  end
end
