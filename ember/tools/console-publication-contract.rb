#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Actual console publication and selected wsdisplay implementation bodies.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'json'
require 'digest'
require 'timeout'

source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
inputs = %w[sys/dev/cons.c sys/dev/cons.h sys/dev/cons_handoffvar.h
            sys/dev/wscons/wsdisplay.c sys/dev/wscons/wsdisplayvar.h
            sys/arch/arm/amlogic/meson_uart.c sys/arch/arm/imx/imxuart.c
            sys/arch/arm/samsung/exynos_uart.c sys/arch/evbarm/dev/plcom.c
            sys/dev/ic/com.c
            ember/tools/console-publication-contract.c
            ember/tools/console-publication-contract.rb]
files = inputs.to_h { |p| [p, File.read(source + '/' + p)] }
# Optional source-selection closure, including conditional branches.
# This is a reviewed finite profile; it does not admit a new publisher.
if ENV['CONSOLE_SELECTED_LIST']
  selected = JSON.parse(File.read(ENV.fetch('CONSOLE_SELECTED_LIST')))
  abort 'selected list must be a nonempty array' unless selected.is_a?(Array) && !selected.empty?
  direct = %w[sys/dev/wsfb/genfb.c sys/arch/arm/fdt/arm_simplefb.c
              sys/dev/pci/viogpu.c sys/dev/wscons/wsdisplay.c
              sys/external/bsd/drm2/radeon/radeon_pci.c]
  parents = %w[sys/dev/wsfb/genfb.c sys/dev/usb/udl.c sys/dev/pci/viogpu.c
               sys/dev/wscons/wsdisplay.c]
  genfb = %w[sys/arch/arm/broadcom/bcm2835_genfb.c sys/arch/arm/fdt/plfb_fdt.c
             sys/external/bsd/drm2/drm/drmfb.c sys/dev/fdt/simplefb.c
             sys/dev/wsfb/genfb.c]
  serial = %w[sys/arch/arm/amlogic/meson_uart.c sys/arch/arm/imx/imxuart.c
              sys/arch/arm/samsung/exynos_uart.c sys/arch/evbarm/dev/plcom.c
              sys/dev/ic/com.c]
  bindings = serial + %w[sys/dev/cons.c sys/dev/wscons/wsdisplay.c
                        sys/arch/evbarm/fdt/fdt_machdep.c]
  rules = {
    'direct' => [/\bwsdisplay_(?:cnattach|preattach|predetach|cndetach)(?:_checked)?\s*\(/, direct],
    'parent' => [/\bconfig_found(?:_acquire)?\s*\([^;]*\bwsemuldisplaydevprint\b|\bwsdisplay_emul_attach_checked\s*\(/m, parents],
    'genfb' => [/\bgenfb_attach\s*\(/, genfb],
    'binding' => [/\bcn_tab\s*=(?!=)|\bcn_tab\s*->\s*cn_dev\s*=(?!=)|\bcn_set_tab(?:_checked)?\s*\(/, bindings]
  }
  seen = Hash.new { |h, k| h[k] = [] }
  selected.each do |path|
    abort 'selected path must be a C source string' unless path.is_a?(String) && path.end_with?('.c')
    text = File.read(File.expand_path(path, source))
    # Discard comments. This remains lexical, not preprocessing proof.
    text = text.gsub(%r{/\*.*?\*/|//[^\n]*}m, '')
    rules.each do |name, (pattern, allowed)|
      next unless text.match?(pattern)
      abort "unreviewed selected #{name} publisher: #{path}" unless allowed.include?(path)
      seen[name] << path
    end
  end
  rules.each do |name, (_, allowed)|
    abort "selected #{name} set changed" unless seen[name].sort == allowed.sort
  end
  puts "console selected publication closure: #{selected.size} C sources PASS"
  exit if ENV['CONSOLE_SELECTION_ONLY'] == '1'
end
def function(text, name)
  matches = text.scan(/^(?:static )?(?:void|int|bool)\n#{Regexp.escape(name)}\(.*?^}/m)
  abort "function count #{name}: #{matches.size}" unless matches.size == 1
  matches.first
end
def block(text, marker)
  lines = text.lines
  starts = lines.each_index.select { |i| lines[i, marker.lines.length].join.chomp == marker }
  abort "block count #{marker}: #{starts.size}" unless starts.size == 1
  depth = 0
  lines[starts.first..].each_with_index do |line, offset|
    depth += 1 if line.match?(/^#\s*(?:if|ifdef|ifndef)\b/)
    depth -= 1 if line.match?(/^#\s*endif\b/)
    return lines[starts.first, offset + 1].join if depth == 0
  end
  abort "unterminated #{marker}"
end
def license(text)
  text.split(/^#(?:include|ifdef|ifndef)/, 2).first
end
console = files.fetch(inputs[0])
display = files.fetch(inputs[3])
publication = block(console, "#ifdef WSDISPLAY_BOOTFB_HANDOFF\n#define CN_PUBLICATION_CLOSED (1U << 31)")
common = function(display, 'wsdisplay_common_attach')
parent = block(display, "#ifdef WSDISPLAY_BOOTFB_HANDOFF\nstatic struct {")
early_body = block(display, "#ifdef WSDISPLAY_EARLYFB_HOOKS\nenum {")
early_api = block(files.fetch('sys/dev/wscons/wsdisplayvar.h'), "#ifdef WSDISPLAY_EARLYFB_HOOKS\n/* Actual binding/replay facts only. No stop, handoff or MMIO right is granted. */")
display_body = [common, parent, *%w[wsdisplay_emul_match wsdisplay_emul_attach
  wsdisplay_emul_detach wsdisplay_cnattach_impl wsdisplay_cnattach_checked
  wsdisplay_preattach_impl wsdisplay_preattach_checked wsdisplay_predetach_impl
  wsdisplay_predetach_checked wsdisplay_cndetach_impl wsdisplay_cndetach_checked].map { |n| function(display, n) }, early_body].join("\n\n")
# Extract only the changed, balanced conditional serial statements.
# The generated wrapper is fixture glue, not a driver implementation.
serials = {
  'meson' => ['sys/arch/arm/amlogic/meson_uart.c', 'meson_uart_consdev'],
  'imx' => ['sys/arch/arm/imx/imxuart.c', 'imxucons'],
  'exynos' => ['sys/arch/arm/samsung/exynos_uart.c', 'exynos_uart_consdev'],
  'plcom' => ['sys/arch/evbarm/dev/plcom.c', 'plcomcons'],
  'com' => ['sys/dev/ic/com.c', 'comcons']
}
serial_body = serials.map do |name, (path, descriptor)|
  text = files.fetch(path)
  start = text.rindex('#ifdef WSDISPLAY_BOOTFB_HANDOFF', text.index(descriptor + '.cn_dev ='))
  abort "serial marker #{name}" unless start
  # The suffix can contain another HANDOFF block. Bound to this balanced one.
  lines = text[start..].lines
  depth = 0
  last = lines.index do |line|
    depth += 1 if line.match?(/^#\s*(?:if|ifdef|ifndef)\b/)
    depth -= 1 if line.match?(/^#\s*endif\b/)
    depth == 0
  end
  abort "serial block #{name}" unless last
  body = lines[0..last].join
  abort "serial identity #{name}" unless body.include?(descriptor + '.cn_dev =')
  license(text) + "\nstatic void serial_#{name}(struct tty *tp, struct fixture_uart *sc, int maj)\n{\n" + body + "}\n"
end.join("\n")
com = files.fetch('sys/dev/ic/com.c')
detach = function(com, 'com_detach')
guard = block(detach, '#ifdef WSDISPLAY_BOOTFB_HANDOFF')
serial_body += "\nstatic int serial_detach(struct fixture_uart *sc, int flags)\n{\n" + guard + "\tdisable_calls++;\n\treturn 0;\n}\n"
evidence = ENV['CONSOLE_EVIDENCE']
abort 'new evidence directory required' if evidence && (File.exist?(evidence) || File.symlink?(evidence))
FileUtils.mkdir_p(evidence) if evidence
commands = []
run = lambda do |label, argv|
  stdout_text = stderr_text = ''
  status = nil
  timed_out = false
  Open3.popen3(*argv, pgroup: true) do |stdin, stdout, stderr, waiter|
    stdin.close
    output_reader = Thread.new { stdout.read }
    error_reader = Thread.new { stderr.read }
    begin
      Timeout.timeout(30) { status = waiter.value }
    rescue Timeout::Error
      timed_out = true
      begin Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
      status = waiter.value
    ensure
      [output_reader, error_reader].each do |reader|
        next if reader.join(2)
        timed_out = true
        begin Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
        abort "reader timeout #{label}" unless reader.join(2)
      end
    end
    stdout_text = output_reader.value
    stderr_text = error_reader.value
  end
  record = { label: label, argv: argv, status: status.exitstatus,
             signal: status.termsig, timed_out: timed_out }
  commands << record
  if evidence
    File.write(evidence + '/' + label + '.stdout', stdout_text)
    File.write(evidence + '/' + label + '.stderr', stderr_text)
    File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "\n")
  end
  print stdout_text
  warn stderr_text unless stderr_text.empty?
  [status, stderr_text, timed_out]
end
cc = Shellwords.split(ENV.fetch('CC', 'cc'))
flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
executor = ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
Dir.mktmpdir('console-publication-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  puts "console publication temporary directory: #{work}"
  originals = { 'publication-body.h' => license(console) + publication,
                'display-body.h' => license(display) + display_body,
                'early-api.h' => license(files.fetch('sys/dev/wscons/wsdisplayvar.h')) + early_api,
                'serial-body.h' => serial_body }
  originals.each { |name, text| File.write(work + '/' + name, text) }
  if evidence
    originals.each { |name, text| File.write(evidence + '/' + name, text) }
    File.write(evidence + '/fixture.c', files.fetch('ember/tools/console-publication-contract.c'))
    File.write(evidence + '/source.json', JSON.pretty_generate({
      source_sha256: inputs.to_h { |p| [p, Digest::SHA256.file(source + '/' + p).hexdigest] },
      extraction: 'Complete actual CAS publication block, selected reservation/parent block, common_attach and named match/attach/detach/checked mutator functions. Original licenses retained.',
      scope: 'Kernel device/config/ref/iterator/Giant/mux/screen/emulator/PMF services are controlled. This does not execute real autoconf detach, kernel MP, close/drain, frontend hardware, or a lease actuator.'
    }) + "\n")
  end
  compile_index = 0
  compile = lambda do
    compile_index += 1
    argv = [*cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-parameter', *flags, '-I' + work,
            source + '/ember/tools/console-publication-contract.c', '-o', work + '/contract']
    status, = run.call("compile-#{compile_index}", argv)
    abort 'fixture compile failed' unless status.success?
  end
  compile.call
  next if ENV['CONSOLE_COMPILE_ONLY'] == '1'
  status, _, timed_out = run.call('positive', executor + [work + '/contract'])
  abort 'positive failed' unless status.success? && !timed_out
  next if ENV['CONSOLE_SKIP_MUTANTS'] == '1'
  mutants = {
    'closed-publication' => ['publication-body.h', 'if ((value & CN_PUBLICATION_CLOSED) != 0)', 'if (false)'],
    'overflow' => ['publication-body.h', 'if ((value & CN_PUBLICATION_COUNT) == CN_PUBLICATION_COUNT)', 'if (false)'],
    'active-freeze' => ['publication-body.h', 'return value == CN_PUBLICATION_CLOSED ? EALREADY : EBUSY;', 'return 0;'],
    'foreign-reservation' => ['display-body.h', 'wsdisplay_console_operation.device != device ||', 'false ||'],
    'thread-reservation' => ['display-body.h', 'wsdisplay_console_operation.lwp != curlwp))', 'false))'],
    'runtime-reservation' => ['display-body.h', 'if (wsdisplay_console_initted >= 2 ||', 'if (false ||'],
    'parent-identity' => ['display-body.h', 'wsdisplay_parent_operation.parent == parent &&', 'true &&'],
    'parent-aux' => ['display-body.h', 'wsdisplay_parent_operation.aux == aux &&', 'true &&'],
    'parent-thread' => ['display-body.h', 'wsdisplay_parent_operation.lwp == curlwp;', 'true;'],
    'false-completion' => ['display-body.h', 'if (sc->sc_attach_done && sc->sc_attach_error == 0)', 'if (true)'],
    'common-gate' => ['display-body.h', 'if (!wsdisplay_console_initted ||', 'if (false ||'],
    'detach-selfhold' => ['display-body.h', 'if (sc->sc_attach_hold)', 'if (false)'],
    'detach-stage' => ['display-body.h', 'if (!sc->sc_attach_initialized)', 'if (false)'],
    'cleanup-identity' => ['display-body.h', 'if (live == child)', 'if (live != NULL)'],
    'primary-error' => ['display-body.h', 'sc->sc_attach_error = error;', 'sc->sc_attach_error = close_error;'],
    'live-ref' => ['display-body.h', 'live = device_lookup_acquire(driver, unit);', 'live = device_lookup_acquire(driver, unit); if (live != NULL) { device_release(live); live = NULL; }'],
    'early-checked-error' => ['display-body.h', "\tif (error != 0)\n\t\tgoto out;\n#else\n\twsdisplay_preattach", "\tif (false)\n\t\tgoto out;\n#else\n\twsdisplay_preattach"],
    'serial-meson' => ['serial-body.h', 'meson_uart_consdev.cn_dev =', 'cn_tab->cn_dev ='],
    'serial-imx' => ['serial-body.h', 'imxucons.cn_dev =', 'cn_tab->cn_dev ='],
    'serial-exynos' => ['serial-body.h', 'exynos_uart_consdev.cn_dev =', 'cn_tab->cn_dev ='],
    'serial-plcom' => ['serial-body.h', 'plcomcons.cn_dev =', 'cn_tab->cn_dev ='],
    'serial-com' => ['serial-body.h', "\t\tcomcons.cn_dev =", "\t\tcn_tab->cn_dev ="],
    'detach-owner' => ['display-body.h', '} else if (wsdisplay_console_operation.device != NULL) {', '} else if (false) {'],
    'serial-detach' => ['serial-body.h', 'if (ISSET(sc->sc_hwflags, COM_HW_CONSOLE))', 'if (false)']
  }
  selected = ENV['CONSOLE_MUTANTS'] ? ENV.fetch('CONSOLE_MUTANTS').split : mutants.keys
  selected.each do |name|
    originals.each { |p, text| File.write(work + '/' + p, text) }
    file, before, after = mutants.fetch(name)
    path = work + '/' + file
    text = File.read(path)
    abort "mutant count #{name}" unless text.scan(before).size == 1
    File.write(path, text.sub(before, after))
    compile.call
    status, stderr_text, timed_out = run.call('mutant-' + name, executor + [work + '/contract'])
    abort "mutant did not reach actual CHECK: #{name}" unless !timed_out &&
      status.signaled? && status.termsig == Signal.list.fetch('ABRT') &&
      stderr_text.include?('console contract failed ')
    puts 'console publication: rejected ' + name
  end
end
