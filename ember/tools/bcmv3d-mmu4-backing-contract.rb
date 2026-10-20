#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV.fetch(0, File.join(__dir__, '../..')))
work = Dir.mktmpdir('bcmv3d-mmu4-backing-', ENV.fetch('TMPDIR', '/tmp'))
puts "Native backing temporary directory: #{work}"
inputs = {}
read = lambda do |path|
  body = File.binread(File.join(source, path))
  inputs[path] = Digest::SHA256.hexdigest(body)
  body
end
write = lambda do |path, body|
  target = File.join(work, path)
  FileUtils.mkdir_p(File.dirname(target))
  File.binwrite(target, body)
end
commands = []
run = lambda do |name, argv, required = true, env = {}|
  out, err, status = Open3.capture3(env, *argv)
  write.call(name + '.stdout', out)
  write.call(name + '.stderr', err)
  commands << {name: name, argv: argv, environment: env, status: status.exitstatus, signal: status.termsig}
  abort "#{name} failed: #{err}" if required && !status.success?
  [out, status, err]
end
begin
  driver = 'sys/external/bsd/drm2/bcmv3d/'
  %w[bcmv3d_mmu4_backing.c bcmv3d_mmu4_backing.h bcmv3d_hvs_prepare.c
     bcmv3d_hvs_prepare.h bcmv3d_drv.h bcmv3d_bo_stats.h].each do |name|
    write.call(name, read.call(driver + name))
  end
  %w[bcm2712_mmu4_table.c bcm2712_mmu4_tablevar.h bcm2712_mmu4_corevar.h
     bcm2712_v3d_render.h].each do |name|
    write.call('arm/broadcom/' + name, read.call('sys/arch/arm/broadcom/' + name))
  end
  fixture = File.join(source, 'ember/tools/bcmv3d-mmu4-backing-contract.c')
  read.call('ember/tools/bcmv3d-mmu4-backing-contract.c')
  # Preserve the real HANDOFF-off profile for this initial-backing fixture.
  write.call('opt_wsdisplay_bootfb.h', "/* HANDOFF-off controlled context. */\n")
  write.call('services.h', "#define BACKING_TYPES\n#include #{fixture.dump}\n#undef BACKING_TYPES\n")
  %w[sys/types.h sys/bus.h sys/param.h sys/errno.h sys/cpu.h sys/kmem.h sys/atomic.h sys/endian.h
     sys/systm.h uvm/uvm_extern.h machine/pmap.h sys/device.h sys/mutex.h sys/vmem.h
     dev/acpi/acpivar.h drm/drm_device.h drm/drm_file.h drm/drm_gem_cma_helper.h
     drm/v3d_drm.h linux/err.h drm/drm_gem.h].each do |name|
    write.call(name, "#include \"services.h\"\n")
  end
  write.call('sys/types.h', "#include_next <sys/types.h>\n#include \"services.h\"\n")
  write.call('sys/errno.h', "#include_next <sys/errno.h>\n")
  # Target libc must finish its own endian/types declarations before doubles.
  write.call('sys/endian.h', "#ifdef __NetBSD__\n#include_next <sys/endian.h>\n#else\n#include \"services.h\"\n#endif\n")
  cc = Shellwords.split(ENV.fetch('CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
  executor = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
  pages = ENV.fetch('BACKING_PAGES', '4096 65536 16384').split
  compile = lambda do |name, page|
    output = File.join(work, 'contract-' + page)
    run.call(name + '-compile-' + page, cc + %w[-std=c11 -Wall -Wextra -Werror] + flags +
      ['-I' + work, '-DPAGE_SIZE=' + page, File.join(work, 'bcmv3d_mmu4_backing.c'),
       File.join(work, 'bcmv3d_hvs_prepare.c'), File.join(work, 'arm/broadcom/bcm2712_mmu4_table.c'),
       fixture, '-o', output])
    output
  end
  pages.each do |page|
    output = compile.call('positive', page)
    if ENV['BACKING_OUTPUT']
      FileUtils.cp(output, ENV.fetch('BACKING_OUTPUT') + '-' + page)
    end
    next if ENV['BACKING_COMPILE_ONLY'] == '1'
    out, = run.call('positive-' + page, executor + [output])
    print out
    if page != '16384'
      out, = run.call('sealed-failure-' + page, executor + [output], true,
                      {'BACKING_SHORT_FAILURES' => '1', 'BACKING_SEALED_FAILURE' => '1'})
      print out
    end
  end
  if ENV['BACKING_COMPILE_ONLY'] != '1' && ENV['BACKING_SKIP_MUTANTS'] != '1'
    path = 'bcmv3d_mmu4_backing.c'
    original = File.binread(File.join(work, path))
    # These controls retain their original initial-backing scope.
    boundary = original.index("\nstatic int\nmmu4_gem_map(")
    legacy = boundary ? original[0...boundary] : original
    staging = boundary ? original[boundary..] : ''
    mutations = {
      'frame-pattern' => ['mmu4_frame_init(b->frame->vaddr);',
                          '(void)mmu4_frame_init;',
                          'memcmp(frame + y * 7680, expected_row, 7680) == 0'],
      'frame-last-row' => ['y < BCMV3D_HVS_HEIGHT;', 'y < BCMV3D_HVS_HEIGHT - 1;',
                           'memcmp(frame + y * 7680, expected_row, 7680) == 0'],
      'frame-band-boundary' => ['x / (BCMV3D_HVS_WIDTH / 8)',
                                'x / (BCMV3D_HVS_WIDTH / 8 + 1)',
                                'memcmp(frame + y * 7680, expected_row, 7680) == 0'],
      'frame-channel' => ['pixel[0] = (uint8_t)color;',
                          'pixel[0] = (uint8_t)(color >> 16);',
                          'memcmp(frame + y * 7680, expected_row, 7680) == 0'],
      'frame-after-sync' => [["\tmmu4_frame_init(b->frame->vaddr);\n",
                              "\tbcmv3d_hvs_prewrite(b->frame);\n"],
                             ['', "\tbcmv3d_hvs_prewrite(b->frame);\n\tmmu4_frame_init(b->frame->vaddr);\n"],
                             'memcmp(frame + y * 7680, expected_row, 7680) == 0'],
      'native-stride' => ['offset += BCM2712_MMU4_PAGE_BYTES', 'offset += PAGE_SIZE'],
      'last-byte' => ['va + offset + 4095,', 'va + offset + 4094,'],
      'parent-address' => ['cma->dmamap->dm_segs[0].ds_addr != pa[0]', 'false'],
      'bounce' => ['(map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0', 'false'],
      'iommu' => ['map->_dm_iommu != NULL', 'false'],
      'control-bound' => ['start > (UINT64_C(1) << 32) - size', 'false'],
      'frame-padding' => ['memset(b->frame->vaddr, 0, b->status.frame_bytes);',
                          'memset(b->frame->vaddr, 0, BCMV3D_HVS_BYTES);',
                          'memcmp(frame + 8294400, zero, padding) == 0'],
      'control-padding' => ['memset(b->control[i].kva, 0, b->status.control_bytes);',
                            'memset(b->control[i].kva, 0, 4096);'],
      'control-sync' => ["b->status.control_bytes, BUS_DMASYNC_PREWRITE);",
                          '4096, BUS_DMASYNC_PREWRITE);'],
      'second-l2' => ['image[2] = b->image.l2[1];', 'image[2] = b->image.l2[0];'],
      'one-shot' => ['b->running || b->status.prepare_started', 'b->running'],
      'reverse-free' => ['for (i = 4; i > 0; i--)', 'for (i = 3; i > 0; i--)'],
      'seal' => ['b->status.sealed = true;', 'b->status.sealed = false;'],
      'mapping-retain' => ['b->status.sealed || b->status.mapping_uncertain', 'b->status.sealed'],
      'commit-output' => ['*out = b;', '/* missing output commit */']
    }
    selected = ENV.fetch('BACKING_MUTANTS', mutations.keys.join(' ')).split
    selected.each do |name|
      old, replacement, expected_check = mutations.fetch(name)
      changed = legacy.dup
      Array(old).zip(Array(replacement)).each do |before, after|
        abort "Non-unique mutant anchor: #{name}" unless changed.scan(before).length == 1
        changed.sub!(before) { after }
      end
      write.call(path, changed + staging)
      rejected = false
      pages.each do |page|
        next if page == '16384'
        output = compile.call(name, page)
        _, status, diagnostic = run.call(name + '-' + page, executor + [output], false,
                            {'BACKING_SHORT_FAILURES' => '1'})
        unless status.success?
          abort "Mutant did not reach expected CHECK: #{name}" unless
            status.signaled? && status.termsig == Signal.list.fetch('ABRT') &&
            diagnostic.include?('CHECK ') &&
            (expected_check.nil? || diagnostic.include?(expected_check))
          rejected = true
        end
      end
      abort "Mutant survived: #{name}" unless rejected
      write.call(path, original)
      puts "PASS: #{name} compiled and rejected"
    end
  end
ensure
  write.call('inputs.json', JSON.pretty_generate(inputs) + "\n")
  write.call('commands.json', JSON.pretty_generate(commands) + "\n")
  if ENV['BACKING_EVIDENCE']
    destination = ENV.fetch('BACKING_EVIDENCE')
    abort 'Evidence output must not exist' if File.exist?(destination)
    FileUtils.cp_r(work, destination)
  end
  FileUtils.remove_entry(work)
end
