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
            sys/dev/wscons/wsdisplayvar.h]
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
def function(text, name)
  matches = text.scan(/^(?:static )?(?:int|void)\n#{Regexp.escape(name)}\(.*?^}/m)
  abort "function count #{name}: #{matches.length}" unless matches.length == 1
  matches.first
end
arm = files.fetch(inputs[0])
state = arm[/^static struct arm_simplefb_softc \{.*?^static bus_space_handle_t arm_simplefb_bsh;/m]
abort 'missing actual ARM state' unless state
registration = files.fetch(inputs[2])[/^#ifdef WSDISPLAY_EARLYFB_HOOKS
enum \{.*?^#endif/m]
abort 'missing actual MI registration' unless registration
api = files.fetch(inputs[4])[/^#ifdef WSDISPLAY_EARLYFB_HOOKS
\/\* Actual binding.*?^#endif/m]
abort 'missing actual MI API' unless api
api += "\nvoid wsdisplay_preattach(const struct wsscreen_descr *, void *, int, int, long);\n"
arm_functions = "#ifdef WSDISPLAY_EARLYFB_HOOKS\n" + function(arm, 'arm_simplefb_replay') + "\n#endif\n" + function(arm, 'arm_simplefb_preattach')
ws_functions = %w[wsdisplay_preattach wsdisplay_cnputc wsdisplay_pollc].map do |name|
  function(files.fetch(inputs[2]), name)
end.join("\n\n")
replay = function(files.fetch(inputs[3]), 'vcons_replay_msgbuf')
cc = Shellwords.split(ENV.fetch('CC', 'cc'))
flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
prefix = ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
Dir.mktmpdir('earlyfb-hooks-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  puts "early framebuffer fixture temporary directory: #{work}"
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
            '-I' + work, source + '/ember/tools/earlyfb-hooks-contract.c',
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
    'panic-ready' => ['mi-state.h', "if (panicstr != NULL)\n\t\treturn EWOULDBLOCK;", 'if (false) return EWOULDBLOCK;'],
'register' => ['mi-state.h', 'WSDISPLAY_EARLY_REGISTERING);', 'WSDISPLAY_EARLY_STARTING);'],
'identity-cookie' => ['wsdisplay-functions.h', 'wsdisplay_console_conf.emulcookie == wsdisplay_early_cookie)', 'true)'],
'identity-screen' => ['wsdisplay-functions.h', 'wsdisplay_console_conf.scrdata == wsdisplay_early_type &&', 'true &&'],
'runtime-initted' => ['wsdisplay-functions.h', 'wsdisplay_console_initted == 1 &&', 'true &&'],
'runtime-device' => ['wsdisplay-functions.h', 'wsdisplay_console_device == NULL &&', 'true &&'],
'repeat-arm' => ['arm-functions.h', 'wsdisplay_early_output_snapshot(&early) == 0)', 'wsdisplay_early_output_snapshot(&early) == 0 && false)'],
'context-mi' => ['mi-state.h', 'panicstr != NULL || cpu_intr_p() || cpu_softintr_p()', 'false'],
'post-binding' => ['mi-state.h', "\tif (!wsdisplay_early_binding())\n\t\treturn ENXIO;\n\tatomic_store_release(&wsdisplay_early_phase, WSDISPLAY_EARLY_READY);", "\tatomic_store_release(&wsdisplay_early_phase, WSDISPLAY_EARLY_READY);"],
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
