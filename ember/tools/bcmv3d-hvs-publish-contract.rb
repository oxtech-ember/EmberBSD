#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV.fetch(0, File.join(__dir__, '../..')))
work = Dir.mktmpdir('bcmv3d-hvs-publish-', ENV.fetch('TMPDIR', '/tmp'))
puts 'HVS publish temporary directory: ' + work
Process.setrlimit(Process::RLIMIT_CORE, 0, 0)
inputs = {}; commands = []
read = lambda do |path|
  text = File.binread(source + '/' + path)
  inputs[path] = Digest::SHA256.hexdigest(text)
  text
end
strip = ->(text) { text.lines.reject { |line| line.start_with?('#include ', '__KERNEL_RCSID(') }.join }
run = lambda do |name, argv, required = true, environment = {}|
  out = ''; err = ''; timeout = false; status = nil
  Open3.popen3(environment, *argv, pgroup: true) do |input, output, error, wait|
    input.close
    readers = [Thread.new { out << output.read }, Thread.new { err << error.read }]
    unless wait.join(Integer(ENV.fetch('HVS_PUBLISH_TIMEOUT', '60')))
      timeout = true
      begin Process.kill('TERM', -wait.pid); rescue Errno::ESRCH; end
      unless wait.join(1)
        begin Process.kill('KILL', -wait.pid); rescue Errno::ESRCH; end
        abort 'test process did not stop' unless wait.join(5)
      end
    end
    readers.each(&:join)
    status = wait.value
  end
  File.binwrite(work + '/' + name + '.stdout', out)
  File.binwrite(work + '/' + name + '.stderr', err)
  commands << {name: name, argv: argv, environment: environment,
               status: status.exitstatus, signal: status.termsig, timeout: timeout}
  abort name + ' failed: ' + err if required && (timeout || !status.success?)
  [out, err, status, timeout]
end
begin
  run.call('native-export', ['ruby', source + '/ember/tools/bcmv3d-mmu4-native-contract.rb', source], true,
           {'MMU4_NATIVE_COMPILE_ONLY' => '1', 'MMU4_NATIVE_EVIDENCE' => work + '/native'})
  native_fixture = read.call('ember/tools/bcmv3d-mmu4-native-contract.c')
  entry = "int\nmain(void)"
  abort 'native fixture main changed' unless native_fixture.scan(entry).size == 1
  File.binwrite(work + '/publish-native-fixture.h', native_fixture.sub(entry, "int\nnative_fixture_reference_main(void)"))
  read.call('ember/tools/bcmv3d-mmu4-native-contract.rb')
  drv = 'sys/external/bsd/drm2/bcmv3d/'
  arm = 'sys/arch/arm/broadcom/'
  files = {
    'publish-types.h' => drv + 'bcmv3d_hvs_publish.h',
    'publish-body.h' => drv + 'bcmv3d_hvs_publish.c',
    'publish-hvs-types.h' => arm + 'bcm2712_display_hvsvar.h',
    'publish-hvs-body.h' => arm + 'bcm2712_display_hvs.c',
    'publish-pv-types.h' => arm + 'bcm2712_display_pv0var.h',
    'publish-pv-body.h' => arm + 'bcm2712_display_pv0.c',
    'publish-hdmi-types.h' => arm + 'bcm2712_display_hdmi0var.h',
    'publish-hdmi-body.h' => arm + 'bcm2712_display_hdmi0.c'
  }
  files.each { |name, path| File.binwrite(work + '/' + name, strip.call(read.call(path))) }
  read.call('ember/tools/bcmv3d-hvs-publish-contract.c')
  read.call('ember/tools/bcmv3d-hvs-publish-contract.rb')
  common = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] + Shellwords.split(ENV.fetch('CFLAGS', ''))
  objects = %w[bcmv3d_mmu4_backing.o bcmv3d_hvs_prepare.o bcm2712_mmu4_table.o bridge.o].map { |name| work + '/native/' + name }
  compile = lambda do |name|
    run.call(name + '-compile', common + ['-I' + work, '-I' + work + '/native',
      '-I' + source + '/sys/external/bsd/acpica/dist/include', '-I' + source + '/ember/tools',
      '-MD', '-MF', work + '/' + name + '.d', source + '/ember/tools/bcmv3d-hvs-publish-contract.c',
      *objects, '-o', work + '/contract'])
  end
  compile.call('positive')
  unless ENV['HVS_PUBLISH_COMPILE_ONLY'] == '1'
    executor = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
    out, = run.call('positive', executor + [work + '/contract'])
    print out
    unless ENV['HVS_PUBLISH_SKIP_MUTANTS'] == '1'
      original = File.binread(work + '/publish-body.h')
      controls = {
        'bypass-selector' => ['bypass = (s->link[8] & 0x7f) == 0x3b;', 'bypass = false;', '0:bypass:0', 'error == expected'],
        'bypass-pipe' => ['EXPECT(BCMV3D_PUBLISH_PV0, 0x94, 0xff0,\n\t    bypass ? 0x100 : pv.pipe_init_ctrl & 0xff0);', 'if (!bypass) EXPECT(BCMV3D_PUBLISH_PV0, 0x94, 0xff0, pv.pipe_init_ctrl & 0xff0);', '0:bypass-mix:3', 'error == expected'],
        'bypass-vfp' => ['CACHED(BCMV3D_PUBLISH_HDMI0, 0xf4, s->timing.hdmi[2],\n\t    0x1f7f1fff, bypass ? 0x05000438 : hdmi.verta0);', 'if (!bypass) CACHED(BCMV3D_PUBLISH_HDMI0, 0xf4, s->timing.hdmi[2], 0x1f7f1fff, hdmi.verta0);', '0:bypass-mix:4', 'error == expected'],
        'bypass-xbar' => ['{ BCMV3D_PUBLISH_DVP0, 0xf4, 0xffffff, 0x354021, 0x543210 }', '{ BCMV3D_PUBLISH_DVP0, 0xf4, 0xffffff, 0x354021, 0x354021 }', '0:bypass:0', 'error == expected'],
        'bypass-inactive-coefficients' => ['if (!bypass) {', 'if (true) {', '0:bypass:1', 'error == expected'],
        'active-coefficients' => ['if (!bypass) {', 'if (false) {', '0:profile:37', 'error == expected'],
        'link-prefix' => ['s->link[i] = s->raw;', 's->link[i] = 0;', '0:positive:0', 'error == expected'],
        'link-cached' => ['CACHED(word->window, word->offset, s->link[i],\n\t\t    word->mask, value);', 'EXPECT(word->window, word->offset, word->mask, word->expected);', '0:positive:0', 'pb.link_reads[i] == 1'],
        'link-read-error' => ['READ(word->window, word->offset);', '(void)publish_read(io, arg, s, word->window, word->offset);', '0:read:30', 'arg == &pb && !pb.faulted && !pb.pending'],
        'native-primary' => ["\tif (n->program_error != 0)\n\t\treturn n->program_error;\n\tif (n->prepare_error != 0)\n\t\treturn n->prepare_error;\n\tif (n->fault != 0)\n\t\treturn n->fault;", "\tif (n->fault != 0)\n\t\treturn n->fault;\n\tif (n->program_error != 0)\n\t\treturn n->program_error;\n\tif (n->prepare_error != 0)\n\t\treturn n->prepare_error;", '0:real-combined:0', 'error == expected'],
        'eof-complete' => ['(s->channel_status & 0x6000) == 0x4000)', '(s->channel_status & 0x4000) != 0)', '0:eof:0', 'error == expected'],
        'firmware-policy' => ['0x00e0003d, pv.control & 0x00e0003d', '0x06fff03d, pv.control & 0x06fff03d', '0:firmware:0', 'error == expected'],
        'ppc2-horza' => ['horza = 0x004a0016;', 'horza = pv.horza;', '0:firmware:0', 'error == expected'],
        'ppc2-horzb' => ['horzb = 0x002c03c0;', 'horzb = pv.horzb;', '1:firmware:0', 'error == expected'],
        'vcontrol-31' => ['s->timing.pv[0], 0xffffffff, value);', 's->timing.pv[0], 0x7fffffff, value);', '0:vcontrol-high:0', 'error == expected'],
        'vcontrol-23' => ['s->timing.pv[0], 0xffffffff, value);', 's->timing.pv[0], 0xff7fffff, value);', '0:vcontrol-high:1', 'error == expected'],
        'vcontrol-interlace' => ['s->timing.pv[0], 0xffffffff, value);', 's->timing.pv[0], 0xffffffef, value);', '0:interlace:0', 'error == expected'],
        'snapshot-prefix' => ['s->timing.pv[i] = s->raw;', 's->timing.pv[i] = 0;', '0:positive:0', 'error == expected'],
        'snapshot-complete' => ['i < __arraycount(hdmi_offsets)', 'i + 1 < __arraycount(hdmi_offsets)', '0:first-timing:0', 'pb.status.timing.words == 12'],
        'snapshot-cached' => ['CACHED(BCMV3D_PUBLISH_PV0, 0xc, s->timing.pv[2], 0xffffffff, horza);', 'EXPECT(BCMV3D_PUBLISH_PV0, 0xc, 0xffffffff, horza);', '0:positive:0', 'pb.timing_reads[i] == (i == 0 ? 2U : 1U)'],
        'snapshot-diagnostic' => ['s->offset = offset;\n\ts->raw = value;', 's->offset = offset ^ 4;\n\ts->raw = value;', '0:first-timing:0', 'pb.status.offset =='],
        'snapshot-read-error' => ['READ(BCMV3D_PUBLISH_PV0, pv_offsets[i]);', '(void)publish_read(io, arg, s, BCMV3D_PUBLISH_PV0, pv_offsets[i]);', '0:read:18', 'arg == &pb && !pb.faulted && !pb.pending'],
        'prepared' => ['!n->backing.prepared ||', 'false ||', '0:native:17', 'pb.status.native_valid && pb.status.native.backing.prepared'],
        'presync' => ['n->backing.syncs != 5 ||', 'false ||', '0:native:21', 'pb.status.native.backing.sealed && pb.status.native.backing.syncs == 5'],
        'dma' => ['frame->dma_base != 0 ||', 'false ||', '0:invalid:0', 'value == plane_word(pb.dlist_writes)'],
        'version-first' => ['READ(BCMV3D_PUBLISH_HVS, 0);', 'READ(BCMV3D_PUBLISH_HVS, 0x20);', '0:positive:0', 'window == 0 && offset == 0'],
        'd0-layout' => ['ctrl = 0x100;', 'ctrl = 0x30;', '1:positive:0', 'window == 1 && offset == 4'],
        'firmware-sram' => ['#define PUBLISH_DLIST\t\t0x4080U', '#define PUBLISH_DLIST\t\t0x4000U', '0:positive:0', 'window == 1 && offset == 4'],
        'lptr-unit' => ['WRITE(BCMV3D_PUBLISH_HVS, lptr, PUBLISH_HEAD);', 'WRITE(BCMV3D_PUBLISH_HVS, lptr, 128);', '0:positive:0', 'pb.barriers == 10 && value == 32'],
        'barrier' => ['io->barrier(arg, window);', 'if (false) io->barrier(arg, window);', '0:positive:0', '!pb.faulted && !pb.pending'],
        'readback' => ['/* Verify only while inactive. HVS can mutate context after activation. */\n\tfor (i = 0; i < BCM2712_HVS_DLIST_WORDS; i++)', '/* Verify only while inactive. HVS can mutate context after activation. */\n\tfor (i = 0; i < BCM2712_HVS_DLIST_WORDS - 1; i++)', '0:positive:0', 'pb.dlist_writes == 10 && pb.dlist_reads == 10'],
        'reset' => ['WRITE(BCMV3D_PUBLISH_HVS, ctrl, 0x40000000);', 'if (false) WRITE(BCMV3D_PUBLISH_HVS, ctrl, 0x40000000);', '0:positive:0', 'pb.reset_writes == 1'],
        'fresh-ctrl1' => ['READ(BCMV3D_PUBLISH_HVS, ctrl + 4);', 'if (false) READ(BCMV3D_PUBLISH_HVS, ctrl + 4);', '0:positive:0', 'pb.channel_writes == 2 && value == 0xa55a0200'],
        'geometry' => ['WRITE(BCMV3D_PUBLISH_HVS, ctrl, 0x877f0437);', 'WRITE(BCMV3D_PUBLISH_HVS, ctrl, 0x87800438);', '0:positive:0', 'value == 0x877f0437'],
        'phy-rate' => ['{ BCMV3D_PUBLISH_RM0, 0x18, 0xffffffff, 0x98100000, 0x98100000 }', '{ BCMV3D_PUBLISH_RM0, 0x18, 0, 0, 0 }', '0:profile:36', 'error == expected'],
        'rgb' => ['{ BCMV3D_PUBLISH_DVP0, 0xf4, 0xffffff, 0x354021, 0x543210 }', '{ BCMV3D_PUBLISH_DVP0, 0xf4, 0, 0, 0 }', '0:profile:24', 'error == expected'],
        'ppc2-crossed-horza' => ['CACHED(BCMV3D_PUBLISH_PV0, 0xc, s->timing.pv[2], 0xffffffff, horza);', 'if (false) CACHED(BCMV3D_PUBLISH_PV0, 0xc, s->timing.pv[2], 0xffffffff, horza);', '1:packing:4', 'error == expected'],
        'ppc2-crossed-horzb' => ['CACHED(BCMV3D_PUBLISH_PV0, 0x10, s->timing.pv[3], 0xffffffff, horzb);', 'if (false) CACHED(BCMV3D_PUBLISH_PV0, 0x10, s->timing.pv[3], 0xffffffff, horzb);', '0:packing:5', 'error == expected'],
        'pv-timing' => ['CACHED(BCMV3D_PUBLISH_PV0, 0x10, s->timing.pv[3], 0xffffffff, horzb);', 'if (false) CACHED(BCMV3D_PUBLISH_PV0, 0x10, s->timing.pv[3], 0xffffffff, horzb);', '0:profile:5', 'error == expected'],
        'hdmi-enable' => ['{ BCMV3D_PUBLISH_HD, 0x44, 0x99040000, 0x80000000, 0x80000000 }', '{ BCMV3D_PUBLISH_HD, 0x44, 0, 0, 0 }', '0:profile:17', 'error == expected'],
        'write-error' => ['error = io->write(arg, window, offset, value);', 'error = io->write(arg, window, offset, value); error = 0;', '0:write:1', '!pb.faulted && pb.pending'],
        'poll-bound' => ['#define PUBLISH_POLLS\t\t100U', '#define PUBLISH_POLLS\t\t99U', '0:timeout:0', 'pb.delays == 100 && pb.status.polls == 100'],
        'sticky' => ['if (s->attempted || s->complete || s->first_error != 0)', 'if (false)', '0:positive:0', '== (error != 0 ? error : EBUSY)']
      }
      selected = ENV.fetch('HVS_PUBLISH_MUTANTS', controls.keys.join(' ')).split
      selected.each do |name|
        before, after, scenario, expected = controls.fetch(name)
        before = before.gsub('\t', "\t").gsub('\n', "\n")
        after = after.gsub('\t', "\t").gsub('\n', "\n")
        abort 'ambiguous control ' + name unless original.scan(before).size == 1
        File.binwrite(work + '/publish-body.h', original.sub(before, after))
        compile.call('mutant-' + name)
        _, err, status, timeout = run.call('mutant-' + name, executor + [work + '/contract'], false,
                                         {'HVS_PUBLISH_SELECT' => scenario})
        fatal = status.signaled? && status.termsig == Signal.list.fetch('ABRT')
        fatal ||= !executor.empty? && status.exitstatus == 128 + Signal.list.fetch('ABRT')
        abort 'not a compiled causal CHECK: ' + name + '\n' + err unless !timeout && fatal && err.include?('ICAC contract failed') && err.include?(expected)
        puts 'Rejected compiled HVS publish mutant ' + name
      end
      File.binwrite(work + '/publish-body.h', original)
    end
  end
ensure
  File.binwrite(work + '/commands.json', JSON.pretty_generate(commands) + "\n")
  File.binwrite(work + '/source.json', JSON.pretty_generate({sha256: inputs,
    extraction: 'Complete actual C/H bodies; include/RCS lines alone removed. The complete existing native fixture main is renamed and reused without edits. Real adapter, ICAC, backing and fixed encoder bodies execute.',
    scope: 'Controlled kernel/bus/allocation services. Native admission, clocks, mappings and real SRAM/UPM/COB ownership are external requirements. The real native boot caller is covered separately by the joined fixture. No hardware proof.'}) + "\n")
  if ENV['HVS_PUBLISH_EVIDENCE']
    dest = ENV.fetch('HVS_PUBLISH_EVIDENCE')
    abort 'new evidence required' if File.exist?(dest)
    FileUtils.mkdir_p(dest)
    FileUtils.cp_r(Dir[work + '/*'], dest)
  end
  FileUtils.remove_entry(work)
end
