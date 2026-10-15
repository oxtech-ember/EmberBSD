# SPDX-License-Identifier: BSD-2-Clause
# Exercise real creation/removal functions and queue macros with controlled allocation/ioctl results.
require 'tmpdir'
require 'open3'
require 'shellwords'
require 'fileutils'
root = File.expand_path(ARGV[0] || '../..', __dir__)
source = File.read(root + '/lib/libnvmm/libnvmm.c')
header = File.read(root + '/lib/libnvmm/nvmm.h')
create = source[/int\nnvmm_machine_create\(.*?^}/m] or abort 'Missing machine creation'
remove = source[/static void\n__area_remove_all\(.*?^}/m] or abort 'Missing area removal'
area = source[/typedef struct __area \{.*?typedef LIST_HEAD\(, __area\) area_list_t;/m] or abort 'Missing area types'
machine = header[/struct nvmm_machine \{.*?^};/m] or abort 'Missing machine descriptor'
code = source[0...source.index('#include')] + File.read(__dir__ + '/nvmm-ownership-fixture.c')
{'MACHINE' => machine, 'AREA' => area, 'PRODUCTION' => create + "\n" + remove}.each { |key, value| code.sub!('@' + key + '@', value) }
Dir.mktmpdir('ember-nvmm-ownership-') do |work|
  FileUtils.cp(root + '/sys/sys/queue.h', work + '/queue.h')
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
