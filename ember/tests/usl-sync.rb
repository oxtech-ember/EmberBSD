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
  'no-irq-pending-flag' => ['sd->s_flags |= flags | SF_NOTIFY;', 'sd->s_flags |= waitok ? flags | SF_NOTIFY : SF_NOTIFY;'],
  'no-owner-check' => ['if (owner)', 'if (owner || true)'],
  'no-death-tombstone' => ['sd->s_flags |= SF_OWNERDEAD;', 'sd->s_flags |= 0;'],
  'no-hook-drain' => ["\t/* The hook writer lock drains callbacks before their argument is freed. */\n\texithook_disestablish(sd->s_exit_hook);", '/* mutation: no exit-hook drain */'],
  'no-ioctl-pin' => ["\t\t\tif (sd) {\n\t\t\t\tusl_sync_hold(sd);", "\t\t\tif (sd) {"],
  'no-post-auth-recheck' => ['error = (sd->s_flags & SF_DYING) ? EINVAL : 0;', 'error = 0;'],
  'no-death-cleanup-after-ack' => ["if (!(sd->s_flags & SF_OWNERDEAD))\n\t\tcallout_stop(&sd->s_notify_ch);", 'callout_stop(&sd->s_notify_ch);'],
  'no-post-lock-death-check' => ["if (sd->s_flags & SF_OWNERDEAD)\n\t\talive = 0;", '/* mutation: ignore owner death */']
}
if (mutation = ENV['USL_MUTATION'])
  from, to = mutations.fetch(mutation) { abort 'Unknown mutation' }
  abort 'Mutation does not match once' unless text.scan(from).size == 1
  text.sub!(from, to)
  if mutation == 'no-ioctl-pin'
    a = text.index('case VT_SETMODE:')
    b = text.index('case VT_GETMODE:', a)
    section = text[a...b]
    abort 'Mutation release does not match once' unless section.scan('usl_sync_rele(sd);').size == 1
    text[a...b] = section.sub('usl_sync_rele(sd);', '/* mutation: no ioctl pin release */')
  end
end
first = text.index('struct usl_syncdata {') or abort 'Missing sync data'
last = text.index("int\nwsdisplay_usl_ioctl1", first) or abort 'Missing ioctl boundary'
owner = ARGV[1] == 'owner'
license = text[0...text.index('#include')]
code = File.read(__dir__ + '/usl-sync-fixture.c')
code.sub!('@LICENSE@', license)
code.sub!('@NSIG@', File.read(root + '/sys/sys/signal.h')[/^#define _NSIG\s+(\d+)/, 1] || abort('Missing _NSIG'))
code.sub!('@FUNCTIONS@', text[first...last])
code.sub!('@IOCTL_FUNCTION@', text[text.index("int\nwsdisplay_usl_ioctl2")..])
support = File.read(__dir__ + '/usl-owner-support.c')
hooks = File.read(root + '/sys/kern/kern_hook.c')
extract = lambda do |name, type|
  a = hooks.index(/^#{name}\(/) or abort "Missing #{name}"
  b = hooks.index(/^}/, a) or abort "Unclosed #{name}"
  type + "\n" + hooks[a..b] + "\n"
end
support.sub!('@HOOK_FUNCTIONS@',
  extract.call('hook_establish', 'static void *') + extract.call('hook_disestablish', 'static void') +
  extract.call('hook_proc_run', 'static void') + extract.call('exithook_establish', 'static void *') +
  extract.call('exithook_disestablish', 'static void') + extract.call('doexithooks', 'static void'))
support.sub!('@HOOK_LICENSE@', hooks[0...hooks.index('#include')])
support.gsub!('@SOURCE_ROOT@', root)
code.sub!('@OWNER_SUPPORT@', support)
code.sub!('@OWNER_TESTS@', owner ? File.read(__dir__ + '/usl-owner-fixture.c') : 'static void owner_tests(void) { abort(); }')
work = ENV.fetch('USL_WORK') { abort 'Set USL_WORK to a private receipt directory' }
FileUtils.mkdir_p(work)
File.write(work + '/fixture.c', code)
inputs = [source, root + '/sys/kern/kern_hook.c', root + '/sys/dev/wscons/wsdisplay_usl_io.h', root + '/sys/sys/signal.h']
File.write(work + '/input.sha256', inputs.map { |p| Digest::SHA256.file(p).hexdigest + "  #{p}\n" }.join)
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
