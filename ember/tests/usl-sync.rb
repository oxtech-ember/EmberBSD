# SPDX-License-Identifier: BSD-2-Clause
# Run the real USL sync functions with controlled kernel boundary hooks.
require 'open3'
require 'shellwords'
require 'fileutils'
require 'digest'

root = File.expand_path(ARGV[0] || '../..', __dir__)
source = ENV.fetch('USL_SOURCE', root + '/sys/dev/wscons/wsdisplay_compat_usl.c')
text = File.read(source)
mutations = {
  'no-generation-check' => ['sd->s_generation != gen', '((void)gen, false)'],
  'no-retirement-pin-check' => ['if (sd->s_refs != 0)', 'if (false)'],
  'no-timeout-deadline-check' => ['if (delta > 0)', 'if (delta > 0 && false)'],
  'no-irq-pending-flag' => ['sd->s_flags |= flags | SF_NOTIFY;', 'sd->s_flags |= waitok ? flags | SF_NOTIFY : SF_NOTIFY;']
}
if (mutation = ENV['USL_MUTATION'])
  from, to = mutations.fetch(mutation) { abort 'Unknown mutation' }
  abort 'Mutation does not match once' unless text.scan(from).size == 1
  text.sub!(from, to)
end
first = text.index('struct usl_syncdata {') or abort 'Missing sync data'
last = text.index("int\nwsdisplay_usl_ioctl1", first) or abort 'Missing ioctl boundary'
license = text[0...text.index('#include')]
code = File.read(__dir__ + '/usl-sync-fixture.c')
code.sub!('@LICENSE@', license)
code.sub!('@NSIG@', File.read(root + '/sys/sys/signal.h')[/^#define _NSIG\s+(\d+)/, 1] || abort('Missing _NSIG'))
code.sub!('@FUNCTIONS@', text[first...last])
work = ENV.fetch('USL_WORK') { abort 'Set USL_WORK to a private receipt directory' }
FileUtils.mkdir_p(work)
File.write(work + '/fixture.c', code)
File.write(work + '/input.sha256', Digest::SHA256.file(source).hexdigest + "  #{source}\n")
cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter'))
out, err, status = Open3.capture3(*cc, *flags, work + '/fixture.c', '-o', work + '/fixture')
File.write(work + '/compile.log', out + err)
abort out + err unless status.success?
out, err, status = Open3.capture3(work + '/fixture', *ARGV.drop(1))
File.write(work + '/run.log', out + err)
puts out
warn err unless err.empty?
exit(status.exitstatus || 128 + status.termsig)
