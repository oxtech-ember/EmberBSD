#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV.fetch(0, File.join(__dir__, '../..')))
work = Dir.mktmpdir('bcmv3d-hvs-prepare-', ENV.fetch('TMPDIR', '/tmp'))
puts "HVS preparation temporary directory: #{work}"
inputs = {}
read = lambda do |path|
  body = File.binread(File.join(source, path))
  inputs[path] = Digest::SHA256.hexdigest(body)
  body
end
write = ->(name, body) { File.binwrite(File.join(work, name), body) }
strip = ->(body) { body.lines.reject { |line| line.start_with?('#include ', '__KERNEL_RCSID(') }.join }
commands = []
run = lambda do |name, argv, must_pass = true|
  out, err, status = Open3.capture3(*argv)
  write.call(name + '.stdout', out)
  write.call(name + '.stderr', err)
  commands << { name: name, argv: argv, status: status.exitstatus }
  abort "#{name} failed: #{err}" if must_pass && !status.success?
  [out, status]
end
begin
  driver = 'sys/external/bsd/drm2/bcmv3d/'
  write.call('bo-body.h', strip.call(read.call(driver + 'bcmv3d_bo.c')))
  write.call('drv-body.h', strip.call(read.call(driver + 'bcmv3d_drv.h')))
  write.call('bo-stats.h', read.call(driver + 'bcmv3d_bo_stats.h'))
  write.call('uapi-body.h', strip.call(read.call('sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h')))
  services = read.call('ember/tools/bcm2712-v3d-bo-contract.c')
  needle = 'struct drm_device { void *dev_private; kmutex_t struct_mutex; };'
  abort 'Changed controlled DRM fixture type' unless services.scan(needle).length == 1
  services = services.sub(needle, <<~C.strip)
    struct drm_minor;
    struct drm_driver;
    struct drm_device {
      void *dev_private, *dev;
      kmutex_t struct_mutex;
      bool registered;
      struct drm_driver *driver;
      struct drm_minor *render;
    };
  C
  services = services.sub('#define PAGE_SIZE 4096', "#ifndef PAGE_SIZE\n#define PAGE_SIZE 4096\n#endif")
  services = services.sub('#include "../../sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h"', '#include "bo-stats.h"')
  write.call('bo-services.h', services)
  write.call('prepare-types.h', strip.call(read.call(driver + 'bcmv3d_hvs_prepare.h')))
write.call('mmu4-core-types.h', strip.call(read.call('sys/arch/arm/broadcom/bcm2712_mmu4_corevar.h')))
write.call('mmu4-backing-types.h', strip.call(read.call(driver + 'bcmv3d_mmu4_backing.h')))
helper = strip.call(read.call(driver + 'bcmv3d_hvs_prepare.c'))
  mod = strip.call(read.call('sys/modules/bcmv3d_hvs_prepare/bcmv3d_hvs_prepare_module.c'))
  # Keep the real BSD round_page and Linux PAGE_MASK interaction in the fixture.
  uvm = read.call('sys/uvm/uvm_param.h')
  linux = read.call('sys/external/bsd/drm2/include/linux/mm.h')
  round = uvm.lines.grep(/^#define\s+round_page\(x\)\s+.*PAGE_MASK/)
  mask = linux.lines.grep(/^#define\s+PAGE_MASK\s+/)
  abort 'Changed native page-macro contract' unless round.length == 1 && mask.length == 1
  write.call('page-macros.h', "#undef round_page\n#undef PAGE_MASK\n" + round[0] + mask[0])
  write.call('prepare-body.h', helper)
  write.call('module-body.h', mod)
  fixture = File.join(source, 'ember/tools/bcmv3d-hvs-prepare-contract.c')
  read.call('ember/tools/bcmv3d-hvs-prepare-contract.c')
  cc = Shellwords.split(ENV.fetch('CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
  executor = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
  libfdt = File.join(source, 'sys/external/bsd/libfdt/dist')
  objects = %w[fdt fdt_ro fdt_rw fdt_sw fdt_wip fdt_empty_tree fdt_strerror fdt_addresses fdt_overlay].map do |unit|
    input = File.join(libfdt, unit + '.c')
    read.call('sys/external/bsd/libfdt/dist/' + unit + '.c')
    output = File.join(work, unit + '.o')
    run.call(unit, cc + %w[-std=c11 -Wall -Wextra] + flags + ['-I' + libfdt, '-c', input, '-o', output])
    output
  end
  compile = lambda do |name|
    run.call(name + '-compile', cc + %w[-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter] + flags +
      ['-I' + work, '-I' + libfdt, fixture, *objects, '-o', File.join(work, 'contract')])
  end
  compile.call('positive')
  FileUtils.cp(File.join(work, 'contract'), ENV['HVS_PREPARE_OUTPUT']) if ENV['HVS_PREPARE_OUTPUT']
  unless ENV['HVS_PREPARE_COMPILE_ONLY'] == '1'
    out, = run.call('positive', executor + [File.join(work, 'contract')])
    print out
    unless ENV['HVS_PREPARE_SKIP_MUTANTS'] == '1'
      mutations = {
        'errno' => [:helper, 'return -PTR_ERR(bo);', 'return PTR_ERR(bo);'],
        'page-mask' => [:helper, "((size_t)BCMV3D_HVS_BYTES + PAGE_SIZE - 1) &\n\t    ~((size_t)PAGE_SIZE - 1)", 'round_page(BCMV3D_HVS_BYTES)'],
        'size' => [:helper, 'cma->dmasize != size', 'false'],
        'bo-size' => [:helper, 'cma->base.size != size', 'false'],
        'map-size' => [:helper, 'frame->map->dm_mapsize != cma->dmasize', 'false'],
        'segment' => [:helper, 'frame->map->dm_segs[0].ds_len != cma->dmasize', 'false'],
        'nseg' => [:helper, 'frame->map->dm_nsegs != 1', 'false'],
        'span' => [:helper, 'address > (UINT64_C(1) << 32) - cma->dmasize', 'false'],
        'address' => [:helper, 'frame->address = address;', 'frame->address = bo->gpu;'],
        'direction' => [:helper, 'BUS_DMA_WAITOK | BUS_DMA_WRITE', 'BUS_DMA_WAITOK'],
        'sync-size' => [:helper, 'frame->map, 0, frame->size,', 'frame->map, 0, BCMV3D_HVS_BYTES - 1,'],
        'sync-kind' => [:helper, 'BUS_DMASYNC_PREWRITE', '0'],
        'unload-order' => [:helper, "\tbus_dmamap_unload(frame->dmat, frame->map);", ''],
        'put' => [:helper, 'drm_gem_object_put_unlocked(&frame->bo->cma.base);', '/* missing constructor put */'],
        'publish-out' => [:helper, '*out = frame;', '/* missing commit */'],
        'fill' => [:module, 'pixels[i] = hvs_prepare_pixel(i);', 'pixels[i] = 0;'],
        'prewrite' => [:module, 'bcmv3d_hvs_prewrite(frame);', '/* missing PREWRITE */'],
        'process' => [:module, 'cpu_intr_p() || cpu_softintr_p()', '(cpu_intr_p() || cpu_softintr_p()) && false'],
        'conflict' => [:module, 'prop_dictionary_get(properties, "phy-mode") != NULL', 'false'],
        'minor-order' => [:module, 'bcmv3d_hvs_destroy(frame);', 'drm_minor_release(&minor); bcmv3d_hvs_destroy(frame);'],
        'mmu4-prepare' => [:module, 'error = bcmv3d_mmu4_backing_prepare(backing, &cfg);', 'error = 0;'],
        'mmu4-destroy' => [:module, 'close_error = bcmv3d_mmu4_backing_destroy_unexposed(backing);', 'close_error = 0;'],
        'mmu4-retain-minor' => [:module, 'hvs_retained_minor = minor;', '/* lost render reference */'],
        'mmu4-retain-code' => [:module, "\t\treturn 0;\n\t}\n\t/* All unexposed maps/GEM refs", "\t\treturn error;\n\t}\n\t/* All unexposed maps/GEM refs"],
        'mmu4-fini' => [:module, "return hvs_retained_backing != NULL || hvs_retained_minor != NULL ?\n\t\t    EBUSY : 0;", 'return 0;'],
        'mmu4-uncertain' => [:module, 'status.mapping_uncertain ||', 'false ||'],
        'mmu4-first-error' => [:module, 'return error != 0 ? error : close_error;', 'return close_error;']
      }
      selected = ENV.fetch('HVS_PREPARE_MUTANTS', mutations.keys.join(' ')).split
      selected.each do |name|
        kind, old, replacement = mutations.fetch(name)
        body = kind == :helper ? helper : mod
        abort "Non-unique mutant anchor: #{name}" unless body.scan(old).length == 1
        write.call(kind == :helper ? 'prepare-body.h' : 'module-body.h', body.sub(old, replacement))
        compile.call(name)
        _, status = run.call(name, executor + [File.join(work, 'contract')], false)
        abort "Mutant survived: #{name}" if status.success?
        write.call('prepare-body.h', helper)
        write.call('module-body.h', mod)
        puts "PASS: #{name} compiled and rejected"
      end
    end
  end
ensure
  write.call('inputs.json', JSON.pretty_generate(inputs) + "\n")
  write.call('commands.json', JSON.pretty_generate(commands) + "\n")
  if ENV['HVS_PREPARE_EVIDENCE']
    destination = ENV.fetch('HVS_PREPARE_EVIDENCE')
    abort 'Evidence output must not exist' if File.exist?(destination)
    FileUtils.cp_r(work, destination)
  end
  FileUtils.remove_entry(work)
end
