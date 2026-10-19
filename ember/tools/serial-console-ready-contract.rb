#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile actual PLCOM readiness, common attachment and frontend bodies.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'digest'
require 'json'
require 'timeout'

source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
abort 'usage: serial-console-ready-contract.rb [SOURCE_ROOT]' if ARGV.size > 1
paths = %w[sys/arch/evbarm/dev/plcom.c sys/arch/evbarm/dev/plcomvar.h
  sys/arch/evbarm/dev/plcomreg.h sys/arch/evbarm/dev/plcom_consolevar.h
  sys/arch/arm/acpi/plcom_acpi.c sys/arch/arm/fdt/plcom_fdt.c
  ember/tools/serial-console-ready-contract.c ember/tools/serial-console-ready-contract.rb]
files = paths.to_h { |path| [path, File.binread(source + '/' + path)] }
def function(text, name)
  found = text.scan(/^(?:static )?(?:void|int|bool)\n#{Regexp.escape(name)}\(.*?^}/m)
  abort "function count #{name}: #{found.size}" unless found.size == 1
  found.first
end
def stripped(text)
  text.lines.reject { |line| line.start_with?('#include ') }.join
end
mi = files.fetch(paths[0])
a = mi.index('static int' + "\t" + 'plcomconsunit = -1;')
b = mi.index('static int ppscap =', a)
abort 'provider and console state not found' unless a && b
originals = {
  'plcomvar.inc' => stripped(files.fetch(paths[1])),
  'plcomreg.inc' => stripped(files.fetch(paths[2])),
  'plcom-console-api.inc' => stripped(files.fetch(paths[3])),
  'provider.inc' => mi[a...b],
  'common.inc' => %w[plcom_attach_subr plcom_config plcom_detach plcom_activate
    plcom_shutdown plcomcnattach plcomcndetach].map { |name| function(mi, name) }.join("\n"),
  'acpi.inc' => %w[plcom_acpi_attach plcom_acpi_cix_deferred].map { |name| function(files.fetch(paths[4]), name) }.join("\n"),
  'fdt.inc' => function(files.fetch(paths[5]), 'plcom_fdt_attach')
}
def notice(text)
  text.split(/^#(?:include|ifdef|ifndef)/,2).first
end
%w[provider.inc common.inc].each { |name| originals[name] = notice(mi) + originals[name] }
originals['acpi.inc'] = notice(files.fetch(paths[4])) + originals.fetch('acpi.inc')
originals['fdt.inc'] = notice(files.fetch(paths[5])) + originals.fetch('fdt.inc')
# Only unused unchanged callback parameters receive fixture declaration attributes.
originals['common.inc'].sub!('plcom_detach(device_t self, int flags)',
  'plcom_detach(device_t self, int flags __attribute__((__unused__)))')
originals['fdt.inc'].sub!('plcom_fdt_attach(device_t parent,',
  'plcom_fdt_attach(device_t parent __attribute__((__unused__)),')
controls = {
  'soft-irq' => ['provider.inc', 'sc->sc_rbuf == NULL || sc->sc_si == NULL ||', 'sc->sc_rbuf == NULL ||'],
  'hard-irq' => ['provider.inc', 'hard_ih != NULL && plcom_console_ready_live(sc, &dev) == 0', 'plcom_console_ready_live(sc, &dev) == 0'],
  'previous' => ['provider.inc', 'if (previous != &plcomcons)', 'if (previous == NULL)'],
  'token' => ['provider.inc', 'if (ready != &plcom_console_ready)', 'if (ready == NULL)'],
  'revoke' => ['provider.inc', 'plcom_console_ready.revoked = true;', 'plcom_console_ready.revoked = false;'],
  'active' => ['provider.inc', '!device_is_active(sc->sc_dev) || !sc->enabled ||', '!sc->enabled ||'],
  'tty-attach' => ['common.inc', "\ttty_attach(tp);", "\t(void)tp;"],
  'acpi-hook' => ['acpi.inc', "\tplcom_console_attach_complete(sc, ih);", "\t(void)ih;"],
  'deferred-hook' => ['acpi.inc', "\tplcom_console_attach_complete(sc, ih);\n#endif\n\taprint_normal_dev", "\t(void)ih;\n#endif\n\taprint_normal_dev"],
  'detach-revoke' => ['common.inc', "\tif (!plcom_console_ready_revoke(NULL)) {\n\t\tif (warm)\n\t\t\tKERNEL_UNLOCK_ONE(NULL);\n\t\treturn;\n\t}\n#endif\n\n\tbus_space_unmap", "#endif\n\n\tbus_space_unmap"],
  'fdt-hook' => ['fdt.inc', "\tplcom_console_attach_complete(sc, ih);", "\t(void)ih;"],
  'incomplete-irq' => ['acpi.inc', '(sc->sc_hwflags & PLCOM_HW_DEV_OK) == 0 || sc->sc_si == NULL', 'false']
}
selected = ENV.fetch('SERIAL_READY_MUTANTS', controls.keys.join(',')).split(',')
abort 'unknown control' unless (selected - controls.keys).empty?
evidence = ENV['SERIAL_READY_EVIDENCE']
abort 'evidence directory must be new' if evidence && (File.exist?(evidence) || File.symlink?(evidence))
FileUtils.mkdir_p(evidence) if evidence
commands = []
run = lambda do |label, argv|
  stdout = stderr = ''; status = nil
  Open3.popen3(*argv, pgroup: true) do |input, output, error, wait|
    input.close
    readers = [Thread.new { output.read }, Thread.new { error.read }]
    begin
      Timeout.timeout(60) { status = wait.value }
    rescue Timeout::Error
      Process.kill('KILL', -wait.pid) rescue Errno::ESRCH
      abort "timeout: #{label}"
    ensure
      readers.each { |reader| abort "reader timeout: #{label}" unless reader.join(2) }
    end
    stdout, stderr = readers.map(&:value)
  end
  commands << { label: label, argv: argv, exitstatus: status.exitstatus, termsig: status.termsig }
  if evidence
    File.write(evidence + '/' + label + '.stdout', stdout)
    File.write(evidence + '/' + label + '.stderr', stderr)
    File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "\n")
  end
  [status, stdout, stderr]
end
cc = Shellwords.split(ENV.fetch('CC', 'cc'))
flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
executor = ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
Dir.mktmpdir('serial-console-ready-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  puts "serial readiness temporary directory: #{work}"
  if evidence
    File.write(evidence + '/sources.json', JSON.pretty_generate(paths.to_h { |p| [p, Digest::SHA256.hexdigest(files.fetch(p))] }) + "\n")
    FileUtils.mkdir_p(evidence + '/actual-inputs')
    originals.each { |name, data| File.binwrite(evidence + '/actual-inputs/' + name, data) }
    File.binwrite(evidence + '/actual-inputs/fixture.c', files.fetch(paths[6]))
  end
  ([nil] + (ENV['SKIP_MUTANTS'] == '1' ? [] : selected)).each do |control|
    inputs = originals.transform_values(&:dup)
    if control
      file, before, after = controls.fetch(control)
      abort "control anchor missing: #{control}" unless inputs.fetch(file).include?(before)
      inputs[file].sub!(before, after)
    end
    inputs.each { |name, data| File.binwrite(work + '/' + name, data) }
    name = control || 'positive'
    binary = work + '/' + name
    args = cc + %w[-std=c11 -Wall -Wextra -Werror] + flags + ['-I' + work, source + '/' + paths[6], '-o', binary]
    status, out, err = run.call(name + '-compile', args)
    abort "compile refused: #{name}\n#{out}#{err}" unless status.success?
    next if ENV['COMPILE_ONLY'] == '1'
    status, out, err = run.call(name + '-run', executor + [binary])
    if control
      abort "control did not reach CHECK: #{control}\n#{out}#{err}" unless status.exitstatus == 1 && err.include?('CHECK line ')
      puts "serial readiness control #{control}: rejected by CHECK"
    else
      abort "positive failed\n#{out}#{err}" unless status.success?
      puts out
    end
  end
end
puts 'serial console readiness contract: PASS'
