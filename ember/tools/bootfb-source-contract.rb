#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual source-fact header and real pinned libfdt.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'json'
require 'digest'
require 'timeout'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
lib = source + '/sys/external/bsd/libfdt/dist'
units = %w[fdt_overlay.c fdt_addresses.c fdt.c fdt_ro.c fdt_rw.c fdt_sw.c fdt_wip.c fdt_empty_tree.c fdt_strerror.c]
inputs = %w[sys/arch/arm/fdt/arm_simplefb.c sys/arch/arm/fdt/arm_simplefb.h sys/arch/evbarm/fdt/fdt_machdep.c sys/sys/sha2.h common/lib/libc/hash/sha2/sha2.c sys/stand/efiboot/efiboot.c sys/stand/efiboot/efiboot.h sys/stand/efiboot/efifdt.c sys/stand/efiboot/efifdt.h sys/external/bsd/gnu-efi/dist/lib/hand.c sys/dev/fdt/bootfb_source.h ember/tools/bootfb-source-contract.c ember/tools/bootfb-source-contract.rb] + units.map { |p| 'sys/external/bsd/libfdt/dist/' + p }
# Extract complete named functions. Keep source lines and licenses in evidence.
# Service types and external functions are controlled in the host fixture.
def function_body(text, name)
  masked = text.gsub(%r{/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'}m) { |x| x.gsub(/[^\n]/, ' ') }
  matches = text.to_enum(:scan, /^((?:static )?[A-Za-z_][\w \t*]*\n#{Regexp.escape(name)}\s*\()/).map { Regexp.last_match.begin(0) }
  abort "function anchor #{name}" unless matches.length == 1
  start = matches.first; open = masked.index('{', start); abort "body #{name}" unless open
  depth = 1; finish = open + 1
  while depth != 0 && finish < masked.size
    depth += 1 if masked[finish] == '{'; depth -= 1 if masked[finish] == '}'
    finish += 1
  end
  abort "unterminated #{name}" unless depth == 0
  [text[start...finish], text[0...start].count("\n") + 1]
end
def emit_function(text, path, name)
  body, line = function_body(text, name)
  "#line #{line} #{path.dump}\n#{body}\n"
end
evidence = ENV['BOOTFB_SOURCE_EVIDENCE']
abort 'new evidence required' if evidence && (File.exist?(evidence) || File.symlink?(evidence))
FileUtils.mkdir_p(evidence) if evidence
commands = []
run = lambda do |label, argv|
  out = err = ''; status = nil; timed_out = false
  Open3.popen3(*argv, pgroup: true) do |i, o, e, waiter|
    i.close; a = Thread.new { o.read }; b = Thread.new { e.read }
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
  commands << {label: label, argv: argv, status: status.exitstatus, signal: status.termsig, timed_out: timed_out}
  if evidence
    File.write(evidence + '/' + label + '.stdout', out); File.write(evidence + '/' + label + '.stderr', err)
    File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "\n")
  end
  puts out unless out.empty?; warn err unless err.empty?
  [status, err, timed_out]
end
Dir.mktmpdir('bootfb-source-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  puts "boot framebuffer source temporary directory: #{work}"
  FileUtils.mkdir_p(work + '/lib/libkern'); File.write(work + '/lib/libkern/libkern.h', "#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n");
  FileUtils.cp(source + '/sys/sys/sha2.h', work + '/sys-sha2.h');
  FileUtils.mkdir_p(work + '/sys'); FileUtils.cp(work + '/sys-sha2.h', work + '/sys/sha2.h'); FileUtils.mkdir_p(work + '/dev/fdt')
  # Host service boundary: standard big-endian loads/stores. Target builds use
  # the actual source header. Literals in the fixture independently check bytes.
  File.write(work + '/sys/endian.h', <<~H)
    #ifndef BOOTFB_SOURCE_TEST_ENDIAN_H
    #define BOOTFB_SOURCE_TEST_ENDIAN_H
    #include <stdint.h>
    #include <stddef.h>
    static inline uint32_t be32dec(const void *p) { const uint8_t *b=p; return (uint32_t)b[0]<<24 | (uint32_t)b[1]<<16 | (uint32_t)b[2]<<8 | b[3]; }
    static inline uint64_t be64dec(const void *p) { const uint8_t *b=p; return (uint64_t)be32dec(b)<<32 | be32dec(b+4); }
    static inline void be32enc(void *p,uint32_t v) { uint8_t *b=p; b[0]=v>>24; b[1]=v>>16; b[2]=v>>8; b[3]=v; }
    static inline void be64enc(void *p,uint64_t v) { uint8_t *b=p; be32enc(b,v>>32); be32enc(b+4,v); }
  #ifndef htobe64
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  #define htobe64(v) __builtin_bswap64(v)
  #else
  #define htobe64(v) (v)
  #endif
  #endif
  #endif
H
  File.write(work + '/sys/errno.h', "#include_next <sys/errno.h>\n")
  FileUtils.cp(source + '/sys/dev/fdt/bootfb_source.h', work + '/dev/fdt/bootfb_source.h')
  fdt_path = source + '/sys/stand/efiboot/efifdt.c'
  boot_path = source + '/sys/stand/efiboot/efiboot.c'
  hand_path = source + '/sys/external/bsd/gnu-efi/dist/lib/hand.c'
  fdt = File.read(fdt_path); boot = File.read(boot_path)
  state_start = fdt.index("#ifdef WSDISPLAY_BOOTFB_HANDOFF\n#include <dev/fdt/bootfb_source.h>")
  state_end = fdt.index("\nstatic EFI_PHYSICAL_ADDRESS initrd_addr", state_start)
  abort 'source state boundary' unless state_start && state_end
  state = fdt[state_start...state_end]
  result = File.read(source + '/sys/stand/efiboot/efiboot.h')[/struct efi_cleanup_result \{.*?\n\};/m]
  abort 'cleanup type' unless result
  notices = [fdt, boot, File.read(hand_path)].map { |text| text[0...text.index('#include')] }.join("\n")
  producer = notices + result + "\nstatic void *fdt_data;\nstatic size_t fdt_data_size = 512 * 1024;\n" + state
  producer += "\nstatic EFI_PHYSICAL_ADDRESS initrd_addr, dtb_addr, rndseed_addr;\nstatic u_long initrd_size, dtb_size, rndseed_size;\n"
  producer += emit_function(File.read(hand_path), hand_path, 'LibLocateHandle')
  %w[efi_fdt_set_data efi_fdt_data efi_fdt_size efi_fdt_overlay_is_compatible efi_fdt_overlay_apply efi_fdt_init efi_fdt_fini efi_fdt_chosen efi_fdt_gop apply_overlay apply_overlay_file load_fdt_overlays efi_fdt_create_acpifdt efi_fdt_runtime_alloc_va efi_fdt_runtime_note efi_fdt_set_virtual_address_map_checked efi_fdt_set_virtual_address_map].each { |n| producer += emit_function(fdt, fdt_path, n) }
  %w[efi_cleanup_checked efi_cleanup].each { |n| producer += emit_function(boot, boot_path, n) }
  producer += emit_function(fdt, fdt_path, 'efi_fdt_prepare_boot')
  File.write(work + '/producer.inc', producer)
arm_path = source + '/sys/arch/arm/fdt/arm_simplefb.c'
mach_path = source + '/sys/arch/evbarm/fdt/fdt_machdep.c'
arm = File.read(arm_path); mach = File.read(mach_path)
capture_start = arm.index("#ifdef WSDISPLAY_BOOTFB_HANDOFF\n/* Source facts only.")
capture_end = arm.index("\nstatic const struct device_compatible_entry", capture_start)
abort 'capture state boundary' unless capture_start && capture_end
declaration = mach[/#define FDT_BUF_SIZE[^\n]*\nstatic uint8_t fdt_data\[FDT_BUF_SIZE\];\n/]
abort 'actual FDT declaration' unless declaration
capture = [arm, mach].map { |text| text[0...text.index('#include')] }.join("\n")
capture += "#line #{arm[0...capture_start].count("\n") + 1} #{arm_path.dump}\n" + arm[capture_start...capture_end]
capture += "\n#define fdt_data bootfb_capture_fdt_data\n" + declaration
capture += emit_function(mach, mach_path, 'fdt_device_register')
capture += "\n#undef fdt_data\n"
File.write(work + '/capture.inc', capture)

  cc = Shellwords.split(ENV.fetch('CC', 'cc')); flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
  # Unchanged libfdt retains all -Wall/-Wextra diagnostics. Its inherited signed
  # comparisons are baseline warnings. The actual fixture also retains inherited
  # loader warnings. Do not suppress a diagnostic category.
  objects = units.map do |unit|
    object = work + '/' + unit + '.o'
    status, _, timeout = run.call('library-' + unit, [*cc, '-std=c11', '-Wall', '-Wextra', *flags, '-Dfdt_setprop=actual_fdt_setprop', '-Dfdt_appendprop=actual_fdt_appendprop', '-Dfdt_add_subnode=actual_fdt_add_subnode', '-Dfdt_pack=actual_fdt_pack', '-I' + lib, '-c', lib + '/' + unit, '-o', object])
    abort "library compile failed #{unit}" unless status.success? && !timeout
    object
  end
sha_object = work + '/sha2.o'
status, _, timeout = run.call('library-sha2', [*cc, '-std=c11', '-Wall', '-Wextra', *flags, '-D_STANDALONE', '-D__KERNEL_RCSID(x,y)=', '-DHAVE_SYS_ENDIAN_H=1', '-I' + work, '-c', source + '/common/lib/libc/hash/sha2/sha2.c', '-o', sha_object])
abort 'actual SHA2 compile failed' unless status.success? && !timeout
objects << sha_object
  # Extracted unchanged loader bodies also retain inherited unused/sign warnings.
  # Keep all diagnostics visible. Native TU checks use the real loader flags.
  argv = [*cc, '-std=c11', '-Wall', '-Wextra', *flags, '-I' + work, '-I' + lib, '-I' + source + '/sys/external/bsd/gnu-efi/dist/inc', source + '/ember/tools/bootfb-source-contract.c', *(ENV['COMPILE_ONLY'] == '1' ? ['-c'] : objects), '-o', work + '/contract']
  status, _, timeout = run.call('compile-positive', argv)
  abort 'compile failed' unless status.success? && !timeout
  if evidence
    inputs.each do |path|
      target = work + '/source-files/' + path
      FileUtils.mkdir_p(File.dirname(target)); FileUtils.cp(source + '/' + path, target)
    end
    FileUtils.cp_r(work, evidence + '/inputs')
    File.write(evidence + '/source.json', JSON.pretty_generate({sha256: inputs.to_h { |p| [p, Digest::SHA256.file(source + '/' + p).hexdigest] }, scope: 'Actual source-fact codec, selected full EFI producer/cleanup/runtime/prepare functions, complete MD capture/registration bodies, real libfdt and actual SHA2. MD FDT storage uses the unchanged declaration with a scoped symbol rename to separate it from the EFI pointer. EFI/storage/allocation and unrelated prepare services are controlled; service ABI is not modeled as a native loader acceptance. Source and extracted line provenance are preserved. No admission, kernel MP, coherent link or hardware claim.'}) + "\n")
  end
  next if ENV['COMPILE_ONLY'] == '1'
  executor = ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
  status, _, timeout = run.call('positive', [*executor, work + '/contract'])
  abort 'positive failed' unless status.success? && !timeout
next if ENV['BOOTFB_SOURCE_SKIP_MUTANTS'] == '1'
header = File.read(work + '/dev/fdt/bootfb_source.h')
changes = {
'md-registration-call' => [:capture, 'arm_simplefb_source_capture(fdt_data, sizeof(fdt_data));', ';', nil, true],
'md-registration-capacity' => [:capture, 'arm_simplefb_source_capture(fdt_data, sizeof(fdt_data));', 'arm_simplefb_source_capture(fdt_data, fdt_totalsize(fdt_data));', nil, true],
'md-registration-order' => [:capture, "#ifdef WSDISPLAY_BOOTFB_HANDOFF\n\t\tarm_simplefb_source_capture(fdt_data, sizeof(fdt_data));\n#endif\n\t\tarm_simplefb_preattach();", "\t\tarm_simplefb_preattach();\n\t\tarm_simplefb_source_capture(fdt_data, sizeof(fdt_data));", nil, true],
'md-cold' => [:capture, '!cold || ', '', nil, true],
'md-giant' => [:capture, '!KERNEL_LOCKED_P() || ', '', nil, true],
'md-panic' => [:capture, 'panicstr != NULL ||', 'false ||', nil, true],
'md-irq' => [:capture, 'cpu_intr_p() || ', '', nil, true],
'md-softirq' => [:capture, 'cpu_softintr_p()', 'false', nil, true],
'md-one-shot' => [:capture, "!=\n\t    ARM_SIMPLEFB_SOURCE_UNATTEMPTED)", "==\n\t    UINT32_MAX)", nil, true],
'md-live-pointer' => [:capture, 'fact->blob != fdtbus_get_data() ||', 'false ||', nil, true],
'md-digest' => [:capture, 'memcmp(digest, fact->digest, sizeof(digest)) != 0', 'false', nil, true],
'md-precommit' => [:capture, 'arm_simplefb_source_revalidate(&value) != 0', 'false', nil, true],
'md-provenance' => [:capture, ["tree.record.producer != BOOTFB_SOURCE_EFI_ACPI64 ||\n\t    tree.record.disposition != BOOTFB_SOURCE_ABSENT ||", "tree.record.producer != BOOTFB_SOURCE_EFI_ACPI64 ||\n\t    tree.record.disposition != BOOTFB_SOURCE_ABSENT)"], ['false ||', 'false)'], nil, true],
'md-full-shape' => [:header, 'bootfb_source_shape(fdt) != 0 ||', 'false ||', nil, true],
  'wire-length' => [:header, 'length != BOOTFB_SOURCE_SIZE', 'length < BOOTFB_SOURCE_SIZE'],
  'wire-reserved' => [:header, 'be32dec(bytes + 12) != 0', 'false'],
  'duplicate-record' => [:header, '++record_count != 1', '++record_count == 0'],
  'record-location' => [:header, 'node != value.chosen || depth != 1 ||', 'false ||'],
  'duplicate-compatible' => [:header, 'compatible_count > 1', 'compatible_count > 2'],
  'empty-compatible' => [:header, 'name == NULL || length <= 0', 'name == NULL'],
    'single-root' => [:header, 'bootfb_source_shape(fdt) != 0 ||', 'false ||'],
    'late-source' => [:header, 'depth == 0 || !properties_allowed', 'depth == 0', 0],
    'late-compatible' => [:header, 'depth == 0 || !properties_allowed', 'depth == 0', 1],
    'named-root' => [:header, 'root_name == NULL || root_length != 0', 'root_name == NULL'],
    'chosen-count' => [:header, 'chosen_count != 1', 'false'],
    'chosen-alias' => [:header, 'depth == 1 && length > 6 &&', 'false &&'],
  'gop-status' => [:producer, 'efi_bootfb_source.status = status;', 'efi_bootfb_source.status = EFI_NOT_FOUND;'],
  'gop-count' => [:producer, 'efi_bootfb_source.count = ngop_handle;', 'efi_bootfb_source.count = 0;'],
  'external-source' => [:producer, '!efi_bootfb_source.overlay_attempted && !efi_bootfb_source.external_dtb &&', '!efi_bootfb_source.overlay_attempted &&'],
  'overlay-history' => [:producer, '!efi_bootfb_source.overlay_attempted && !efi_bootfb_source.external_dtb &&', '!efi_bootfb_source.external_dtb &&'],
  'protocol-error' => [:producer, 'efi_bootfb_source.protocol_failed || efi_bootfb_source.fdt_error != 0 ||', 'efi_bootfb_source.fdt_error != 0 ||'],
  'fdt-write-error' => [:producer, 'efi_bootfb_source.protocol_failed || efi_bootfb_source.fdt_error != 0 ||', 'efi_bootfb_source.protocol_failed ||'],
  'runtime-fdt-error' => [:producer, 'cleanup->runtime_fdt_error != 0)', 'false)'],
  'runtime-status' => [:producer, 'EFI_ERROR(cleanup->runtime_status) ||', 'false ||'],
  'runtime-framebuffer' => [:producer, 'record.disposition == BOOTFB_SOURCE_ABSENT && tree.framebuffer', 'false'],
  'external-neutralize' => [:producer, 'old != NULL && length == BOOTFB_SOURCE_SIZE', 'false'],
  'finish-before-cleanup' => [:producer, "\tefi_cleanup_checked(&cleanup);", "\tmemset(&cleanup, 0, sizeof(cleanup));\n\tefi_fdt_source_finish(&cleanup);\n\tefi_cleanup_checked(&cleanup);"]
}
selected = ENV['BOOTFB_SOURCE_MUTANTS'] ? ENV.fetch('BOOTFB_SOURCE_MUTANTS').split(',') : changes.keys
abort 'unknown mutant' unless (selected - changes.keys).empty?
selected.each do |name|
  target, from, to, late_case, md_only = changes.fetch(name)
  text = {header: header, producer: producer, capture: capture}.fetch(target).dup
  Array(from).zip(Array(to)).each do |before, after|
    abort "mutant anchor #{name}" unless text.scan(Regexp.new(Regexp.escape(before))).length == 1
    text.sub!(before) { after }
  end
  File.write(work + '/dev/fdt/bootfb_source.h', target == :header ? text : header)
  File.write(work + '/producer.inc', target == :producer ? text : producer)
  File.write(work + '/capture.inc', target == :capture ? text : capture)
  st, _, late = run.call('compile-' + name, argv + ['-DNDEBUG', *(late_case.nil? ? [] : ['-DBOOTFB_SOURCE_LATE_CASE=' + late_case.to_s]), *(md_only ? ['-DBOOTFB_SOURCE_MD_ONLY=1'] : [])])
  abort "mutant compile failed #{name}" unless st.success? && !late
  st, diagnostic, late = run.call('mutant-' + name, [*executor, work + '/contract'])
  abort "mutant did not reach CHECK #{name}" unless !late && st.signaled? && st.termsig == Signal.list.fetch('ABRT') && diagnostic.include?('bootfb source CHECK ')
end
File.write(work + '/dev/fdt/bootfb_source.h', header)
File.write(work + '/producer.inc', producer)
File.write(work + '/capture.inc', capture)

end
