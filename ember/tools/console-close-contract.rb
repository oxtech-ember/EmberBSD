#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Actual present-lease close bodies; kernel scheduling and FDT services controlled.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'json'
require 'digest'
require 'timeout'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
inputs = %w[sys/dev/cons.c sys/dev/cons_handoffvar.h sys/dev/wscons/wsdisplay.c
  sys/dev/wscons/wsdisplayvar.h sys/dev/wscons/wsdisplay_bootfb.c
  sys/dev/wscons/wsdisplay_bootfbvar.h sys/dev/fdt/simplefb.c
  sys/dev/fdt/simplefbvar.h sys/arch/arm/fdt/arm_simplefb.c
  sys/dev/wscons/wsdisplay_vcons_util.c sys/kern/subr_prf.c ember/tools/console-close-contract.c
  ember/tools/console-close-contract.rb]
files = inputs.to_h { |p| [p, File.read(source + '/' + p)] }
def function(text, name)
  a = text.scan(/^(?:static )?(?:void|int|bool)\n#{Regexp.escape(name)}\(.*?^}/m)
  abort "function count #{name}: #{a.size}" unless a.size == 1
  a.first
end
def block(text, marker)
  lines = text.lines
  found = lines.each_index.select { |i| lines[i, marker.lines.size].join.chomp == marker }
  abort "block count #{marker}: #{found.size}" unless found.size == 1
  depth = 0
  lines[found.first..].each_with_index do |line, offset|
    depth += 1 if line.match?(/^#\s*(?:if|ifdef|ifndef)\b/)
    depth -= 1 if line.match?(/^#\s*endif\b/)
    return lines[found.first, offset + 1].join if depth == 0
  end
  abort "unterminated #{marker}"
end
def license(text)
  text.split(/^#(?:include|ifdef|ifndef)/, 2).first
end
ws = files.fetch('sys/dev/wscons/wsdisplay.c')
fb = files.fetch('sys/dev/fdt/simplefb.c')
arm = files.fetch('sys/arch/arm/fdt/arm_simplefb.c')
api = block(files.fetch('sys/dev/wscons/wsdisplayvar.h'), "#ifdef WSDISPLAY_EARLYFB_HOOKS\n/* Actual binding/replay facts only. No stop, handoff or MMIO right is granted. */")
state = ws[/^static uint64_t wsdisplay_console_generation;\nstatic bool wsdisplay_poll_admitted;/]
operation = ws[/^static struct \{\n\tdevice_t device;\n\tstruct lwp \*lwp;\n\tunsigned int depth;\n} wsdisplay_console_operation;/]
abort 'actual binding state missing' unless state && operation
fbstate = fb[/^struct simplefb_softc \{.*?^};/m]
armstate = block(arm, "#ifdef WSDISPLAY_BOOTFB_DEFER\n/* Written only by the real preattach, then published once after replay. */")
display = %w[wsdisplay_preattach_impl wsdisplay_preattach_checked].map { |n| function(ws, n) }.join("\n")
display += "\n" + block(ws, "#ifdef WSDISPLAY_EARLYFB_HOOKS\nenum {")
display += "\n" + %w[wsdisplay_early_binding_matches wsdisplay_early_close wsdisplay_cnputc wsdisplay_getc wsdisplay_pollc].map { |n| function(ws, n) }.join("\n")
# These three unchanged callback parameters are unused in the kernel source.
# Annotate only their extracted declarations. Keep all bodies and -Wextra.
%w[wsdisplay_cnputc wsdisplay_getc wsdisplay_pollc].each do |name|
  before = name + '(dev_t dev'
  abort "unused callback parameter #{name}" unless display.scan(Regexp.new(Regexp.escape(before))).size == 1
  display.sub!(before, name + '(dev_t dev __attribute__((__unused__))')
end
originals = {

  'publication.h' => license(files.fetch('sys/dev/cons.c')) + block(files.fetch('sys/dev/cons.c'), "#ifdef WSDISPLAY_BOOTFB_HANDOFF\n#define CN_PUBLICATION_CLOSED (1U << 31)"),
  'binding-state.h' => state + "\n" + operation + "\n",
  'early-api.h' => api + "\nint wsdisplay_preattach_checked(const struct wsscreen_descr *, void *, int, int, long);\n",
  'display.h' => license(ws) + display,
  'issuer.c' => files.fetch('sys/dev/wscons/wsdisplay_bootfb.c'),
  'simplefb-state.h' => license(fb) + fbstate,
  'arm-binding.h' => license(arm) + armstate + "\n" + function(arm, 'fdtbus_bootfb_match'),
  'fdt-close.h' => license(fb) + %w[simplefb_console_claim simplefb_console_cancel].map { |n| function(fb, n) }.join("\n") + "\n" + block(fb, "#ifdef WSDISPLAY_BOOTFB_HANDOFF\n/* This wrapper owns the unified result, including a failed post-drain rematch. */"),
  'putone.h' => license(files.fetch('sys/kern/subr_prf.c')) + function(files.fetch('sys/kern/subr_prf.c'), 'putone'),
  'replay.h' => license(files.fetch('sys/dev/wscons/wsdisplay_vcons_util.c')) + function(files.fetch('sys/dev/wscons/wsdisplay_vcons_util.c'), 'vcons_replay_msgbuf')
}
evidence = ENV['CONSOLE_CLOSE_EVIDENCE']
abort 'new evidence directory required' if evidence && (File.exist?(evidence) || File.symlink?(evidence))
FileUtils.mkdir_p(evidence) if evidence
commands = []
run = lambda do |label, argv|
  out = err = ''; status = nil; timed_out = false
  Open3.popen3(*argv, pgroup: true) do |i, o, e, waiter|
    i.close
    a = Thread.new { o.read }; b = Thread.new { e.read }
    begin
      Timeout.timeout(30) { status = waiter.value }
    rescue Timeout::Error
      timed_out = true
      begin Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
      status = waiter.value
    ensure
      [a, b].each do |reader|
        next if reader.join(2)
        timed_out = true
        begin Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
        abort "reader timeout #{label}" unless reader.join(2)
      end
    end
    out = a.value; err = b.value
  end
  commands << { label: label, argv: argv, status: status.exitstatus, signal: status.termsig, timed_out: timed_out }
  if evidence
    File.write(evidence + '/' + label + '.stdout', out)
    File.write(evidence + '/' + label + '.stderr', err)
    File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "\n")
  end
  puts out unless out.empty?
  warn err unless err.empty?
  [status, err, timed_out]
end
cc = Shellwords.split(ENV.fetch('CC', 'cc'))
flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
executor = ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
Dir.mktmpdir('console-close-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  puts "console close temporary directory: #{work}"
  %w[sys dev/wscons dev].each { |p| FileUtils.mkdir_p(work + '/' + p) }
  File.write(work + '/sys/types.h', "#include_next <sys/types.h>\n#include <stdint.h>\n#include <stdbool.h>\n")
  File.write(work + '/sys/device.h', "#ifndef FIXTURE_DEVICE\n#define FIXTURE_DEVICE\nstruct device; typedef struct device *device_t;\n#endif\n")
  File.write(work + '/sys/bus.h', "#ifndef FIXTURE_BUS\n#define FIXTURE_BUS\n#include <stdint.h>\ntypedef uint64_t bus_addr_t, bus_size_t; typedef uintptr_t bus_space_handle_t;\nstruct bus_space { int (*bs_map)(void *, bus_addr_t, bus_size_t, int, bus_space_handle_t *); };\ntypedef struct bus_space *bus_space_tag_t;\n#endif\n")
  File.write(work + '/sys/pserialize.h', "#ifndef FIXTURE_PSZ\n#define FIXTURE_PSZ\nstruct pserialize { int cookie; }; typedef struct pserialize *pserialize_t;\n#endif\n")
  File.write(work + '/sys/errno.h', "#include_next <sys/errno.h>\n")
  %w[param cpu kernel systm].each { |n| File.write(work + '/sys/' + n + '.h', '') }
  File.write(work + '/dev/cons_handoffvar.h', files.fetch('sys/dev/cons_handoffvar.h'))
  File.write(work + '/dev/wscons/wsdisplay_bootfbvar.h', files.fetch('sys/dev/wscons/wsdisplay_bootfbvar.h'))
  File.write(work + '/wsdisplay_bootfbvar.h', files.fetch('sys/dev/wscons/wsdisplay_bootfbvar.h'))
  File.write(work + '/simplefbvar.h', files.fetch('sys/dev/fdt/simplefbvar.h'))
  originals.each { |p, t| File.write(work + '/' + p, t) }
  if evidence
    originals.each { |p, t| File.write(evidence + '/' + p, t) }
    File.write(evidence + '/fixture.c', files.fetch('ember/tools/console-close-contract.c'))
    FileUtils.mkdir_p(evidence + '/inputs')
    Dir.children(work).each { |p| FileUtils.cp_r(work + '/' + p, evidence + '/inputs/' + p) }
    File.write(evidence + '/source.json', JSON.pretty_generate({
      source_sha256: inputs.to_h { |p| [p, Digest::SHA256.hexdigest(files.fetch(p))] },
      extraction: 'Only the three existing unused callback dev parameters carry a fixture-only unused attribute; bodies are unchanged. Complete actual issuer C, CAS block, binding state, named wsdisplay/FDT matcher/wrapper functions, actual initial replay and final vcons update sequence. Original licenses retained.',
      scope: 'Kernel Giant/pserialize/allocation/context/device and FDT parser/platform services are controlled. The pthread grace-period model does not execute kernel MP synchronization. No detach, hardware access, native caller, sink replacement or verified-absence admission.'
    }) + "\n")
  end
  index = 0
  compile = lambda do |label|
    index += 1
    argv = [*cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread', *flags,
            '-I' + work, source + '/ember/tools/console-close-contract.c', '-o', work + '/contract']
    s, _, timeout = run.call("compile-#{index}-#{label}", argv)
    abort "compile failed #{label}" unless s.success? && !timeout
    FileUtils.cp(work + '/contract', evidence + '/' + label + '.bin') if evidence
  end
  compile.call('positive')
  unless ENV['COMPILE_ONLY'] == '1'
    s, _, timeout = run.call('positive', [*executor, work + '/contract'])
    abort 'positive failed' unless s.success? && !timeout
  end
  next if ENV['SKIP_MUTANTS'] == '1' || ENV['COMPILE_ONLY'] == '1'
  mutants = {
    'mi-read-context' => ['issuer.c', "if (pserialize_in_read_section())\n\t\treturn EDEADLK;", 'if (false) return EDEADLK;'],
    'reader-closed' => ['display.h', 'if (cn_publication_closed())\n\t\tgoto out;'.gsub('\\n', "\n").gsub('\\t', "\t"), 'if (false) goto out;'],
    'writer-barrier' => ['display.h', 'pserialize_perform(psz);', 'if (false) pserialize_perform(psz);'],
    'giant-release' => ['display.h', 'KERNEL_UNLOCK_ALL(NULL, &count);', 'count = 0; if (false) KERNEL_UNLOCK_ALL(NULL, &count);'],
    'giant-restore' => ['display.h', 'KERNEL_LOCK(count, NULL);', '(void)count;'],
    'post-generation' => ['display.h', 'generation != wsdisplay_console_generation ||', 'false ||'],
    'post-panic' => ['display.h', "if (panicstr != NULL) {\n\t\t/* perform may", "if (false) {\n\t\t/* perform may"],
    'freeze-count' => ['publication.h', 'atomic_cas_uint(&cn_publication, 0,', 'atomic_cas_uint(&cn_publication, cn_publication,'],
    'getc-admission' => ['display.h', 'if (cn_publication_enter() != 0)', 'if (false)'],
    'poll-admission' => ['display.h', 'wsdisplay_poll_admitted = cn_publication_enter() == 0;', 'wsdisplay_poll_admitted = true;'],
    'poll-hold' => ['display.h', 'if (!on && wsdisplay_poll_admitted) {', 'if (wsdisplay_poll_admitted) {'],
    'replay-closed' => ['display.h', 'if (cn_publication_closed())\n\t\terror = EBUSY;'.gsub('\\n', "\n").gsub('\\t', "\t"), 'if (false) error = EBUSY;'],
    'mi-error' => ['issuer.c', 'value.error = error;', 'value.error = 0;'],
    'terminal-phase' => ['issuer.c', "BOOTFB_CLOSED : BOOTFB_CLAIMED;", "BOOTFB_CLAIMED : BOOTFB_CLAIMED;"],
    'md-rematch' => ['fdt-close.h', "value.md_error = fdtbus_bootfb_match(dev,\n\t\t\t    sc->sc_phandle, sc->sc_bst, &sc->sc_framebuffer);", 'value.md_error = 0;'],
    'md-sticky' => ['fdt-close.h', 'simplefb_close.terminal) {', 'false) {']
  }
  selected = ENV['MUTANT'] ? ENV.fetch('MUTANT').split(',') : mutants.keys
  selected.each do |name|
    target, before, after = mutants.fetch(name)
    text = originals.fetch(target)
    abort "mutant anchor #{name}" unless text.scan(Regexp.new(Regexp.escape(before))).size == 1
    File.write(work + '/' + target, text.sub(before, after))
    compile.call(name)
    status, stderr, timeout = run.call(name, [*executor, work + '/contract'])
    abort "mutant not a causal CHECK #{name}" unless !timeout && status.termsig == Signal.list.fetch('ABRT') && stderr.include?('close CHECK ')
    originals.each { |p, t| File.write(work + '/' + p, t) }
    puts "console close causal mutant #{name}: PASS"
  end
end
