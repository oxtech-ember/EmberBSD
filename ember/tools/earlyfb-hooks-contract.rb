#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile actual boot console bodies. Kernel/rasops services are controlled.
require 'tmpdir'
require 'fileutils'
require 'shellwords'
require 'digest'
require 'json'
require 'open3'
require 'timeout'
source = File.realpath(File.expand_path(ARGV[0] || '../..', ARGV[0] ? Dir.pwd : __dir__))
inputs = %w[sys/arch/arm/fdt/arm_simplefb.c sys/arch/arm/fdt/arm_simplefb.h
            sys/dev/wscons/wsdisplay.c sys/dev/wscons/wsdisplay_vcons_util.c
            sys/dev/wscons/wsdisplayvar.h sys/dev/fdt/simplefb_subr.c
            sys/dev/fdt/simplefbvar.h]
files = inputs.to_h { |p| [p, File.read(source + '/' + p)] }
licenses = files.to_h { |p, body| [p, body.split(/^#(?:include|ifndef|ifdef _KERNEL_OPT)/, 2).first] }
commands = []
evidence = ENV['EARLYFB_EVIDENCE']
if evidence
  abort 'Evidence directory must not exist' if File.exist?(evidence) || File.symlink?(evidence)
  FileUtils.mkdir_p(evidence)
end
run = lambda do |label, args|
  out = err = ''
  status = nil
  Open3.popen3(*args, pgroup: true) do |input, stdout, stderr, waiter|
    input.close
    output_reader = Thread.new { stdout.read }
    error_reader = Thread.new { stderr.read }
    begin
      Timeout.timeout(30) { status = waiter.value }
    rescue Timeout::Error
      Process.kill('KILL', -waiter.pid)
      status = waiter.value
    end
    out = output_reader.value
    err = error_reader.value
  end
  commands << { label: label, argv: args, status: status.exitstatus, signal: status.termsig }
  if evidence
    File.write(evidence + '/' + label + '.stdout', out)
    File.write(evidence + '/' + label + '.stderr', err)
    File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "
")
  end
  print out
  warn err unless err.empty?
  status.success?
end
# Retain nested option branches in the actual outer source block.
def conditional(text, marker)
  lines = text.lines
  starts = lines.each_index.select { |i| lines[i, marker.lines.length].join.chomp == marker }
  abort "conditional count #{marker}: #{starts.length}" unless starts.length == 1
  first = starts.first
  depth = 0
  lines[first..].each_with_index do |line, offset|
    depth += 1 if line.match?(/^#\s*(?:if|ifdef|ifndef)\b/)
    depth -= 1 if line.match?(/^#\s*endif\b/)
    return lines[first, offset + 1].join if depth.zero?
  end
  abort "unterminated conditional #{marker}"
end

def function(text, name)
  matches = text.scan(/^(?:static )?(?:int|void)\n#{Regexp.escape(name)}\(.*?^}/m)
  abort "function count #{name}: #{matches.length}" unless matches.length == 1
  matches.first
end
arm = files.fetch(inputs[0])
state = arm[/^static struct arm_simplefb_softc \{.*?^static bus_addr_t arm_simplefb_addr;/m]
abort 'missing actual ARM state' unless state
registration = conditional(files.fetch(inputs[2]), "#ifdef WSDISPLAY_EARLYFB_HOOKS\nenum {")
abort 'missing actual MI registration' unless registration
api = files.fetch(inputs[4])[/^#ifdef WSDISPLAY_EARLYFB_HOOKS
\/\* Actual binding.*?^#endif/m]
abort 'missing actual MI API' unless api
api += "\nvoid wsdisplay_preattach(const struct wsscreen_descr *, void *, int, int, long);\n"
arm_functions = "#ifdef WSDISPLAY_EARLYFB_HOOKS\n" + function(arm, 'arm_simplefb_replay') + "\n#endif\n" + function(arm, 'arm_simplefb_preattach')
ws_functions = %w[wsdisplay_preattach_impl wsdisplay_preattach wsdisplay_cnputc wsdisplay_pollc].map do |name|
  function(files.fetch(inputs[2]), name)
end.join("\n\n")
replay = function(files.fetch(inputs[3]), 'vcons_replay_msgbuf')
cc = Shellwords.split(ENV.fetch('CC', 'cc'))
flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
prefix = ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
Dir.mktmpdir('earlyfb-hooks-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  puts "early framebuffer fixture temporary directory: #{work}"
FileUtils.mkdir_p(work + '/sys')
FileUtils.mkdir_p(work + '/dev/fdt')
File.write(work + '/sys/bus.h', <<~H)
  #ifndef EARLYFB_TEST_BUS_H
  #define EARLYFB_TEST_BUS_H
  #include <stdint.h>
  typedef unsigned long bus_addr_t, bus_size_t, fixture_paddr_t;
  typedef uintptr_t bus_space_handle_t;
  struct bus_space { int token; int (*bs_map)(void *, bus_addr_t, bus_size_t, int, bus_space_handle_t *); };
  typedef struct bus_space *bus_space_tag_t;
  #define paddr_t fixture_paddr_t
  #endif
H
File.write(work + '/sys/device.h', "typedef void *device_t;\n")
File.write(work + '/sys/param.h', "#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <limits.h>\n#define PAGE_SIZE 4096\n")
File.write(work + '/sys/errno.h', "#include_next <sys/errno.h>\n")
File.write(work + '/sys/systm.h', "#include <string.h>\n")
File.write(work + '/dev/fdt/fdtvar.h', <<~H)
  int OF_finddevice(const char *); int OF_parent(int); int OF_getproplen(int, const char *);
  int fdtbus_get_addr_cells(int); int fdtbus_get_size_cells(int);
  int fdtbus_get_reg64(int, int, uint64_t *, uint64_t *);
  int of_getprop_uint32(int, const char *, uint32_t *);
  const char *fdtbus_get_string(int, const char *);
H
FileUtils.cp(source + '/sys/dev/fdt/simplefbvar.h', work + '/simplefbvar.h')
FileUtils.cp(source + '/sys/dev/fdt/simplefbvar.h', work + '/dev/fdt/simplefbvar.h')
FileUtils.cp(source + '/sys/dev/fdt/simplefb_subr.c', work + '/simplefb_subr.c')
  File.write(work + '/early-api.h', licenses.fetch(inputs[4]) + api)
  File.write(work + '/mi-state.h', licenses.fetch(inputs[2]) + registration + "\n")
  File.write(work + '/arm-state.h', licenses.fetch(inputs[0]) + state + "\n")
  File.write(work + '/arm-functions.h', licenses.fetch(inputs[0]) + arm_functions + "\n")
  File.write(work + '/wsdisplay-functions.h', licenses.fetch(inputs[2]) + ws_functions + "\n")
  File.write(work + '/replay-function.h', licenses.fetch(inputs[3]) + replay + "\n")
  saved = %w[mi-state.h arm-functions.h wsdisplay-functions.h].to_h { |name| [name, File.read(work + '/' + name)] }
  compilation = 0
  compile = lambda do |enabled, multi = true|
    compilation += 1
    args = [*cc, '-std=c11', '-Wall', '-Werror', '-pthread', *flags,
            *(multi ? ['-DWSDISPLAY_MULTICONS'] : []), *(enabled ? ['-DWSDISPLAY_EARLYFB_HOOKS'] : []),
            '-I' + work, work + '/simplefb_subr.c', source + '/ember/tools/earlyfb-hooks-contract.c',
            '-o', work + '/contract']
    puts Shellwords.join(args)
    abort 'fixture compile failed' unless run.call("compile-#{compilation}-#{enabled}-#{multi}", args)
  end
[false, true].product([false, true]).each do |enabled, multi|
  compile.call(enabled, multi)
  next if ENV['EARLYFB_COMPILE_ONLY'] == '1'
  abort 'positive failed' unless run.call("positive-#{enabled}-#{multi}", prefix + [work + '/contract'])
end
  if ENV['EARLYFB_EVIDENCE']
    to = evidence
    Dir.glob(work + '/*.h').each { |p| FileUtils.cp(p, to) }
    FileUtils.cp(source + '/ember/tools/earlyfb-hooks-contract.c', to + '/fixture.c')
    FileUtils.cp(work + '/simplefb_subr.c', to + '/simplefb_subr.c')
    %w[sys dev].each { |d| FileUtils.cp_r(work + '/' + d, to + '/' + d) }
    File.write(to + '/source.json', JSON.pretty_generate({
      source_sha256: (inputs + %w[ember/tools/earlyfb-hooks-contract.c ember/tools/earlyfb-hooks-contract.rb]).to_h { |p| [p, Digest::SHA256.file(source + '/' + p).hexdigest] },
      extraction: 'Unchanged complete named functions and exact static state/API blocks. Original licenses are preserved. Only kernel/FDT/rasops/atomic/pserialize services are controlled.',
      scope: 'Actual functions/state/header, controlled FDT/rasops/atomic/pserialize services. No stop, grace-period writer, handoff, real kernel concurrency or hardware proof.'
    }) + "\n")
  end
  next if ENV['EARLYFB_COMPILE_ONLY'] == '1' || ENV['EARLYFB_SKIP_MUTANTS'] == '1'
  mutants = {
    'reader-enter' => ['wsdisplay-functions.h', 'early_s = pserialize_read_enter();', 'early_s = 0;'],
    'reader-exit' => ['wsdisplay-functions.h', "if (early_section)\n\t\tpserialize_read_exit(early_s);", 'if (early_section) (void)early_s;'],
    'outer-replay' => ['mi-state.h', 's = pserialize_read_enter();', 's = 0;'],
    'ready-early' => ['mi-state.h', "\treplay(arg);", "\tatomic_store_release(&wsdisplay_early_phase, WSDISPLAY_EARLY_READY);\n\treplay(arg);"],
    'context' => ['arm-functions.h', 'panicstr != NULL || cpu_intr_p() || cpu_softintr_p()', 'false'],
    'panic-ready' => ['mi-state.h', 'if (panicstr != NULL) {', 'if (false) {'],
'register' => ['mi-state.h', 'WSDISPLAY_EARLY_REGISTERING);', 'WSDISPLAY_EARLY_STARTING);'],
'identity-cookie' => ['wsdisplay-functions.h', 'wsdisplay_console_conf.emulcookie == wsdisplay_early_cookie)', 'true)'],
'identity-screen' => ['wsdisplay-functions.h', 'wsdisplay_console_conf.scrdata == wsdisplay_early_type &&', 'true &&'],
'runtime-initted' => ['wsdisplay-functions.h', 'wsdisplay_console_initted == 1 &&', 'true &&'],
'runtime-device' => ['wsdisplay-functions.h', 'wsdisplay_console_device == NULL &&', 'true &&'],
'repeat-arm' => ['arm-functions.h', 'wsdisplay_early_output_snapshot(&early) == 0)', 'wsdisplay_early_output_snapshot(&early) == 0 && false)'],
'context-mi' => ['mi-state.h', 'panicstr != NULL || cpu_intr_p() || cpu_softintr_p()', 'false'],
'post-binding' => ['mi-state.h', "\tif (!wsdisplay_early_binding()) {\n\t\terror = ENXIO;\n\t\tgoto out;\n\t}\n\tatomic_store_release(&wsdisplay_early_phase, WSDISPLAY_EARLY_READY);", "\tatomic_store_release(&wsdisplay_early_phase, WSDISPLAY_EARLY_READY);"],
'once' => ['mi-state.h', 'WSDISPLAY_EARLY_STARTING) != WSDISPLAY_EARLY_NONE)', 'WSDISPLAY_EARLY_STARTING) != WSDISPLAY_EARLY_NONE && false)']
  }
  selected = ENV['EARLYFB_MUTANTS'] ? ENV.fetch('EARLYFB_MUTANTS').split : mutants.keys
  selected.each do |name|
    saved.each { |p, bytes| File.write(work + '/' + p, bytes) }
    path, before, after = mutants.fetch(name)
    text = File.read(work + '/' + path)
    abort "mutant anchor #{name}" unless text.scan(before).length == 1
    File.write(work + '/' + path, text.sub(before, after))
    compile.call(true)
    log = work + '/mutant-' + name + '.log'
    accepted = run.call('mutant-' + name, prefix + [work + '/contract'])
    abort 'surviving mutant: ' + name if accepted
    abort 'mutant did not reach the fatal check: ' + name unless commands.last.fetch(:signal) == Signal.list.fetch('ABRT')
    File.write(log, 'The exact stdout/stderr are retained by the command recorder.')
    puts 'early framebuffer hooks: rejected ' + name
  end
end
