#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Actual FDT/ARM/MI/finalizer chain. No display or grace-period actuator.
require 'tmpdir'
require 'fileutils'
require 'shellwords'
require 'json'
require 'digest'
require 'open3'
require 'timeout'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
inputs = %w[sys/dev/fdt/simplefb.c sys/dev/fdt/simplefb_subr.c sys/dev/fdt/simplefbvar.h
            sys/dev/fdt/simplefb_bootvar.h sys/dev/wscons/wsdisplay.c
            sys/dev/wscons/wsdisplay_bootfb.c sys/dev/wscons/wsdisplay_bootfbvar.h
            sys/dev/wscons/wsdisplayvar.h sys/arch/arm/fdt/arm_simplefb.c
            sys/arch/evbarm/fdt/fdt_bus_machdep.c sys/kern/subr_autoconf.c
            sys/dev/wscons/wsdisplay_vcons_util.c ember/tools/earlyfb-hooks-contract.c
            ember/tools/bootfb-issuer-contract.c ember/tools/bootfb-issuer-contract.rb]
files = inputs.to_h { |p| [p, File.read(source + '/' + p)] }
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
  matches = text.scan(/^(?:static )?(?:int|void|bool|device_t)\n#{Regexp.escape(name)}\(.*?^}/m)
  abort "function count #{name}: #{matches.size}" unless matches.size == 1
  matches.first
end
def license(text)
  text.split(/^#(?:include|ifdef|ifndef)/, 2).first
end
evidence = ENV['BOOTFB_EVIDENCE']
abort 'new evidence directory required' if evidence && File.exist?(evidence)
FileUtils.mkdir_p(evidence) if evidence
commands = []
last_stderr = ''
run = lambda do |label, argv|
  out = err = ''
  status = nil
  Open3.popen3(*argv, pgroup: true) do |i, o, e, waiter|
    i.close
    a = Thread.new { o.read }; b = Thread.new { e.read }
    begin
      Timeout.timeout(30) { status = waiter.value }
    rescue Timeout::Error
      Process.kill('KILL', -waiter.pid); status = waiter.value
    end
    out = a.value; err = b.value
  end
  commands << { label: label, argv: argv, status: status.exitstatus, signal: status.termsig }
  if evidence
    File.write(evidence + '/' + label + '.stdout', out)
    File.write(evidence + '/' + label + '.stderr', err)
    File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "\n")
  end
  last_stderr = err
  print out; warn err unless err.empty?
  status
end
Dir.mktmpdir('bootfb-issuer-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  puts "boot framebuffer fixture temporary directory: #{work}"
  saved = nil
  begin
    %w[sys dev/fdt dev/wscons].each { |d| FileUtils.mkdir_p(work + '/' + d) }
    File.write(work + '/sys/bus.h', <<~H)
      #ifndef BOOTFB_TEST_BUS_H
      #define BOOTFB_TEST_BUS_H
      #include <stdint.h>
      #ifdef BOOTFB_BUS32
      typedef uint32_t bus_addr_t, bus_size_t, fixture_paddr_t;
      #else
      typedef uint64_t bus_addr_t, bus_size_t, fixture_paddr_t;
      #endif
      typedef uintptr_t bus_space_handle_t;
      struct bus_space { int token; int (*bs_map)(void *, bus_addr_t, bus_size_t, int, bus_space_handle_t *); };
      typedef struct bus_space *bus_space_tag_t;
      #define paddr_t fixture_paddr_t
      #define bs_map_proto(f) int f ## _bs_map(void *, bus_addr_t, bus_size_t, int, bus_space_handle_t *)
      #endif
    H
    File.write(work + '/sys/device.h', <<~H)
      #ifndef BOOTFB_TEST_DEVICE_H
      #define BOOTFB_TEST_DEVICE_H
      struct device; typedef struct device *device_t;
      struct cfattach;
      void *device_private(device_t);
      bool device_is_a(device_t, const char *);
      struct cfattach *device_cfattach(device_t);
      #endif
    H
    File.write(work + '/sys/param.h', "#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include <limits.h>\n#define PAGE_SIZE 4096\n")
    File.write(work + '/sys/errno.h', "#include_next <sys/errno.h>\n")
    File.write(work + '/sys/systm.h', "#include <string.h>\n")
    %w[cpu kernel].each { |n| File.write(work + '/sys/' + n + '.h', '') }
    File.write(work + '/dev/fdt/fdtvar.h', <<~H)
      int OF_finddevice(const char *); int OF_parent(int); int OF_getproplen(int, const char *);
      int fdtbus_get_addr_cells(int); int fdtbus_get_size_cells(int);
      int fdtbus_get_reg64(int, int, uint64_t *, uint64_t *);
      int of_getprop_uint32(int, const char *, uint32_t *);
      const char *fdtbus_get_string(int, const char *);
    H
    %w[simplefbvar.h simplefb_bootvar.h].each do |n|
      FileUtils.cp(source + '/sys/dev/fdt/' + n, work + '/' + n)
      FileUtils.cp(source + '/sys/dev/fdt/' + n, work + '/dev/fdt/' + n)
    end
    FileUtils.cp(source + '/sys/dev/wscons/wsdisplay_bootfbvar.h', work + '/wsdisplay_bootfbvar.h')
    FileUtils.cp(source + '/sys/dev/wscons/wsdisplay_bootfbvar.h', work + '/dev/wscons/wsdisplay_bootfbvar.h')
    FileUtils.cp(source + '/sys/dev/fdt/simplefb_subr.c', work + '/simplefb_subr.c')
    FileUtils.cp(source + '/sys/dev/wscons/wsdisplay_bootfb.c', work + '/bootfb-core.c')
    arm = files.fetch('sys/arch/arm/fdt/arm_simplefb.c')
    state = arm[/^static struct arm_simplefb_softc \{.*?(?=^static const struct device_compatible_entry)/m]
    compat = arm[/^static const struct device_compatible_entry compat_data\[\].*?^};/m]
    abort 'actual ARM state absent' unless state && compat
    File.write(work + '/arm-state.h', license(arm) + state + compat + "\n")
    arm_body = function(arm, 'arm_simplefb_find_node') + "\n#ifdef WSDISPLAY_EARLYFB_HOOKS\n" + function(arm, 'arm_simplefb_replay') + "\n#endif\n" + function(arm, 'arm_simplefb_preattach') + "\n#ifdef WSDISPLAY_BOOTFB_DEFER\n" + %w[fdtbus_bootfb_match fdtbus_bootfb_defer].map { |n| function(arm, n) }.join("\n") + "\n#endif\n"
    File.write(work + '/arm-functions.h', license(arm) + arm_body)
    ws = files.fetch('sys/dev/wscons/wsdisplay.c')
    state = conditional(ws, "#ifdef WSDISPLAY_EARLYFB_HOOKS\nenum {")
    abort 'actual MI state absent' unless state
    File.write(work + '/mi-state.h', license(ws) + state + "\n")
    api = files.fetch('sys/dev/wscons/wsdisplayvar.h')[/^#ifdef WSDISPLAY_EARLYFB_HOOKS\n\/\* Actual binding.*?^#endif/m]
    abort 'actual API absent' unless api
    File.write(work + '/early-api.h', api + "\nvoid wsdisplay_preattach(const struct wsscreen_descr *, void *, int, int, long);\n")
    ws_body = %w[wsdisplay_preattach_impl wsdisplay_preattach wsdisplay_cnputc wsdisplay_pollc].map { |n| function(ws, n) }.join("\n")
    ws_body += "\n#ifdef WSDISPLAY_BOOTFB_DEFER\n" + function(ws, 'wsdisplay_early_binding_matches') + "\n#endif\n"
    File.write(work + '/wsdisplay-functions.h', license(ws) + ws_body)
    replay = files.fetch('sys/dev/wscons/wsdisplay_vcons_util.c')
    File.write(work + '/replay-function.h', license(replay) + function(replay, 'vcons_replay_msgbuf'))
    md = files.fetch('sys/arch/evbarm/fdt/fdt_bus_machdep.c')
    tail = md[/^#ifdef WSDISPLAY_BOOTFB_DEFER\n#include <dev\/fdt\/simplefb_bootvar.h>.*\z/m]
    abort 'actual MD predicate absent' unless tail
    File.write(work + '/md-functions.h', license(md) + function(md, 'nonposted_mmio_bs_map') + "\n" + tail)
    finalizer = files.fetch('sys/kern/subr_autoconf.c')
    state = finalizer[/^struct finalize_hook \{.*?^static int config_finalize_done;/m]
    abort 'actual finalizer state absent' unless state
    File.write(work + '/finalizer-functions.h', license(finalizer) + state + "\n" + %w[config_finalize_register config_finalize].map { |n| function(finalizer, n) }.join("\n"))
    fb = files.fetch('sys/dev/fdt/simplefb.c')
    File.write(work + '/simplefb-state.h', license(fb) + fb[/^struct simplefb_softc \{.*?^};/m])
    names = %w[simplefb_read_framebuffer simplefb_console_reserve simplefb_console_get_framebuffer simplefb_console_release simplefb_console_commit simplefb_console_fallback simplefb_attach_genfb simplefb_attach]
    body = names.map { |n| function(fb, n) }.join("\n")
    body += "\n#ifdef WSDISPLAY_BOOTFB_DEFER\n" + %w[simplefb_console_claim simplefb_console_cancel].map { |n| function(fb, n) }.join("\n") + "\n#endif\n"
    File.write(work + '/simplefb-functions.h', license(fb) + body)
    saved = Dir.glob(work + '/*.{h,c}').to_h { |p| [p, File.binread(p)] }
    cc = Shellwords.split(ENV.fetch('CC', 'cc'))
    flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
    prefix = ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
    compile = lambda do |label, native, narrow, legacy|
      argv = [*cc, '-std=c11', '-Wall', '-Werror', '-pthread', *flags, '-DBOOTFB_ISSUER_CONTRACT', '-DWSDISPLAY_EARLYFB_HOOKS',
              *(native ? ['-DWSDISPLAY_BOOTFB_DEFER'] : []), *(narrow ? ['-DBOOTFB_BUS32'] : []), *(legacy ? ['-DVIRTGPU_CONSOLE'] : []),
              '-I' + work, '-I' + source + '/ember/tools', work + '/simplefb_subr.c', source + '/ember/tools/bootfb-issuer-contract.c', '-o', work + '/contract']
      abort 'fixture compile failed: ' + label unless run.call('compile-' + label, argv).success?
    end
    native_variants = ENV['BOOTFB_LEGACY_ONLY'] == '1' ? [false] : [false, true]
    variants = ENV['BOOTFB_SKIP_POSITIVES'] == '1' ? [] : native_variants.product([false, true], [false, true])
    variants.each do |native, narrow, legacy|
      label = "#{native}-#{narrow}-#{legacy}"
      compile.call(label, native, narrow, legacy)
      next if ENV['BOOTFB_COMPILE_ONLY'] == '1'
      abort 'positive failed: ' + label unless run.call('positive-' + label, [*prefix, work + '/contract']).success?
    end
    expected_checks = {
      "narrow-first" => "simplefb_read_node(1, &arm_generic_bs_tag, &out) != 0",
      "schema" => "simplefb_read_node(1, &arm_generic_bs_tag, &out) != 0",
      "reg-length" => "simplefb_read_node(1, &arm_generic_bs_tag, &out) != 0",
      "page-end" => "simplefb_read_node(1, &arm_generic_bs_tag, &out) != 0",
      "known-map" => "!fdtbus_bootfb_cpu_map(&copy)",
      "capture-success" => "maps == 1 && updates == 0 && arm_simplefb_captured == 0",
      "source-blob" => "fdtbus_bootfb_match(&display, 1, attachment_tag, &checked) != 0",
      "full-aperture" => "fdtbus_bootfb_match(&display, 1, attachment_tag, &checked) != 0",
      "map-present" => "fdtbus_bootfb_match(&display, 1, attachment_tag, &checked) != 0",
      "domain-before-helper" => "fixture_get_reg_calls == 0 && maps == 0 && genfb_attaches == 0",
      "immediate-window" => "genfb_attaches == 1 && wsdisplay_bootfb.phase == BOOTFB_FALLBACK_DONE",
      "claimed-finalizer-return" => "wsdisplay_bootfb_finalize(&display) == 0",
      "claim-binding" => "simplefb_console_claim(&lease, &fb) != 0 && lease == (void *)1",
      "cancel-binding" => "simplefb_console_cancel(lease) != 0 && wsdisplay_bootfb.phase == BOOTFB_RETAINED",
      "legacy-identity" => "simplefb_console_get_framebuffer(&foreign, &fb) == EPERM",
      "legacy-committed" => "!simplefb_console_release(&foreign)",
      "fallback-before-effects" => "wsdisplay_bootfb_test_phase() == 4",
      "lease-once" => "simplefb_console_claim(&saved_lease, &fb) != 0"
    }
    unless ENV['BOOTFB_SKIP_MUTANTS'] == '1' || ENV['BOOTFB_COMPILE_ONLY'] == '1'
      mutants = {
        'narrow-first' => ['simplefb_subr.c', nil, "error = simplefb_check_range(address, size);", "address = (bus_addr_t)address; size = (bus_size_t)size;\n\terror = simplefb_check_range(address, size);"],
        'schema' => ['simplefb_subr.c', nil, "if ((address_cells != 1 && address_cells != 2) ||\n\t    (size_cells != 1 && size_cells != 2))", 'if (address_cells < 0 || size_cells < 0)'],
        'reg-length' => ['simplefb_subr.c', nil, 'length != (address_cells + size_cells) * 4', 'length < 0'],
        'page-end' => ['simplefb_subr.c', nil, 'end > UINT64_MAX - mask', 'false'],
        'known-map' => ['md-functions.h', nil, 'arm_generic_bs_tag.bs_map == BOOTFB_CPU_MAP', 'true'],
        'capture-success' => ['arm-functions.h', 'arm_simplefb_preattach', 'if (error == 0)', 'if (error >= 0)'],
        'source-blob' => ['arm-functions.h', 'fdtbus_bootfb_match', 'arm_simplefb_boot.blob != fdtbus_get_data() ||', 'false ||'],
        'full-aperture' => ['arm-functions.h', 'fdtbus_bootfb_match', 'fb.sf_size != arm_simplefb_boot.size ||', 'false ||'],
        'map-present' => ['arm-functions.h', 'fdtbus_bootfb_match', '!arm_simplefb_boot.map_present ||', 'false ||'],
        'domain-before-helper' => ['simplefb-functions.h', 'simplefb_attach', 'simplefb_node_cpu_domain(phandle)', '0'],
        'immediate-window' => ['bootfb-core.c', 'wsdisplay_bootfb_register', 'else if (wsdisplay_bootfb.phase == BOOTFB_REGISTERING)', 'else if (wsdisplay_bootfb.phase != BOOTFB_RETAINED)'],
        'claimed-finalizer-return' => ['bootfb-core.c', 'wsdisplay_bootfb_finalize', "wsdisplay_bootfb.pending = true;\n\t\treturn 0;", "wsdisplay_bootfb.pending = true;\n\t\treturn 1;"],
        'claim-binding' => ['bootfb-core.c', 'wsdisplay_bootfb_claim', '!wsdisplay_bootfb_binding()', 'false'],
        'cancel-binding' => ['bootfb-core.c', 'wsdisplay_bootfb_cancel', '!wsdisplay_bootfb_binding()', 'false'],
        'legacy-identity' => ['bootfb-core.c', 'wsdisplay_bootfb_legacy_device', 'wsdisplay_bootfb.legacy_owner != owner', 'false'],
        'legacy-committed' => ['bootfb-core.c', 'wsdisplay_bootfb_legacy_device', 'wsdisplay_bootfb.committed', 'false'],
        'fallback-before-effects' => ['bootfb-core.c', 'wsdisplay_bootfb_finalize', 'wsdisplay_bootfb.phase = BOOTFB_FALLBACK_STARTING;', 'wsdisplay_bootfb.phase = BOOTFB_DEFERRED;'],
        'lease-once' => ['bootfb-core.c', 'wsdisplay_bootfb_claim', 'wsdisplay_bootfb_lease.issued)', 'false)']
      }
      selected = ENV['BOOTFB_MUTANTS'] ? ENV.fetch('BOOTFB_MUTANTS').split : mutants.keys
      selected.each do |name|
        saved.each { |p, bytes| File.binwrite(p, bytes) }
        path, fn, before, after = mutants.fetch(name)
        text = File.read(work + '/' + path)
        target = fn ? function(text, fn) : text
        abort 'mutant anchor count: ' + name unless target.scan(before).size == 1
        changed = target.sub(before, after)
        File.write(work + '/' + path, fn ? text.sub(target, changed) : changed)
        if evidence
          FileUtils.mkdir_p(evidence + '/mutants/' + name)
          FileUtils.cp(work + '/' + path, evidence + '/mutants/' + name + '/' + path)
        end
        compile.call('mutant-' + name, true, name != 'page-end', true)
        status = run.call('mutant-' + name, [*prefix, work + '/contract'])
        check_line = last_stderr.lines.any? { |line| line.start_with?('check failed ') && line.include?(': ' + expected_checks.fetch(name)) }
        abort 'mutant did not reach expected fatal CHECK: ' + name unless status.termsig == Signal.list.fetch('ABRT') && check_line
        puts 'boot framebuffer issuer: rejected ' + name
      end
      saved.each { |p, bytes| File.binwrite(p, bytes) }
    end

  ensure
    saved&.each { |p, bytes| File.binwrite(p, bytes) }
    if evidence
      FileUtils.mkdir_p(evidence + '/inputs')
      Dir.glob(work + '/**/*').select { |p| File.file?(p) && File.basename(p) != 'contract' }.each do |p|
        to = evidence + '/inputs/' + p.delete_prefix(work + '/')
        FileUtils.mkdir_p(File.dirname(to)); FileUtils.cp(p, to)
      end
      files.each do |p, bytes|
        to = evidence + '/source-inputs/' + p
        FileUtils.mkdir_p(File.dirname(to)); File.write(to, bytes)
      end
      File.write(evidence + '/source.json', JSON.pretty_generate({source_sha256: inputs.to_h { |p| [p, Digest::SHA256.hexdigest(files.fetch(p))] },
        scope: 'Actual unmodified parser/core C and real headers; unchanged complete ARM/simplefb/wsdisplay/finalizer/replay functions/state extracted with original notices. Reused existing reader services. Controlled Giant/FDT/map/configuration services; no real MP, hardware, close or drain proof.'}) + "\n")
    end
  end
end
