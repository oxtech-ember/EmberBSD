#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV.fetch(0, File.join(__dir__, '../..')))
work = Dir.mktmpdir('bcmv3d-gem-scanout-', ENV.fetch('TMPDIR', '/tmp'))
puts "GEM scanout temporary directory: #{work}"
Process.setrlimit(Process::RLIMIT_CORE, 0, 0)
inputs = {}; commands = []
read = lambda do |path|
  s = File.binread(File.join(source, path)); inputs[path] = Digest::SHA256.hexdigest(s); s
end
write = lambda do |name, s|
  p = File.join(work, name); FileUtils.mkdir_p(File.dirname(p)); File.binwrite(p, s)
end
replace = lambda do |s, old, new|
  abort "non-unique service anchor: #{old}" unless s.scan(old).size == 1
  s.sub(old) { new }
end
strip = ->(s) { s.lines.reject { |l| l.start_with?('#include ', '__KERNEL_RCSID(') }.join }
timeout_seconds = Integer(ENV.fetch('SCANOUT_TIMEOUT', '60'))
abort 'SCANOUT_TIMEOUT must be positive' unless timeout_seconds.positive?
run = lambda do |name, argv, required = true, env = {}, limit = timeout_seconds|
  o = ''; e = ''; st = nil; timed_out = false; pid = nil
  Open3.popen3(env, *argv, pgroup: true) do |input, output, error, wait|
    pid = wait.pid
    input.close
    readers = [Thread.new { o << output.read }, Thread.new { e << error.read }]
    unless wait.join(limit)
      timed_out = true
      begin Process.kill('TERM', -pid); rescue Errno::ESRCH; end
      unless wait.join(1)
        begin Process.kill('KILL', -pid); rescue Errno::ESRCH; end
        abort 'owned test did not terminate' unless wait.join(5)
      end
    end
    readers.each do |reader|
      next if reader.join(1)
      timed_out = true
      begin Process.kill('KILL', -pid); rescue Errno::ESRCH; end
      output.close unless output.closed?
      error.close unless error.closed?
      reader.kill unless reader.join(1)
    end
    st = wait.value
  end
  write.call(name + '.stdout', o); write.call(name + '.stderr', e)
  commands << {name: name, argv: argv, environment: env, pid: pid,
               status: st.exitstatus, signal: st.termsig, timeout: timed_out}
  abort "#{name}: #{e}" if required && (timed_out || !st.success?)
  [o, e, st, timed_out]
end
fatal_check = lambda do |diagnostic, status, timed_out|
  !timed_out && status.signaled? && status.termsig == Signal.list.fetch('ABRT') && diagnostic.include?('CHECK ')
end
begin
  drv = 'sys/external/bsd/drm2/bcmv3d/'
  %w[bcmv3d_drv.h bcmv3d_bo_stats.h bcmv3d_hvs_prepare.h bcmv3d_mmu4_backing.h].each do |f|
    write.call(f, strip.call(read.call(drv + f)))
  end
  {'bo-body.h' => 'bcmv3d_bo.c', 'backing-body.h' => 'bcmv3d_mmu4_backing.c',
   'hvs-body.h' => 'bcmv3d_hvs_prepare.c'}.each do |dst, src|
    write.call(dst, strip.call(read.call(drv + src)))
  end
  write.call('drv-body.h', File.read(work + '/bcmv3d_drv.h'))
  write.call('uapi-body.h', strip.call(read.call('sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h')))
  %w[bcm2712_mmu4_tablevar.h bcm2712_mmu4_corevar.h].each do |f|
    write.call(f, read.call('sys/arch/arm/broadcom/' + f))
  end
  write.call('table-body.h', strip.call(read.call('sys/arch/arm/broadcom/bcm2712_mmu4_table.c')))
  services = read.call('ember/tools/bcm2712-v3d-bo-contract.c').split('#include "bo-body.h"').first
  services = replace.call(services, '#include "../../sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h"', '#include "bcmv3d_bo_stats.h"')
  services = replace.call(services, '#define PAGE_SIZE 4096', "#ifndef PAGE_SIZE\n#define PAGE_SIZE 4096\n#endif")
  services = replace.call(services, '#define KASSERT assert', '#define KASSERT CHECK')
  services = replace.call(services, 'size_t dm_mapsize; bus_dma_segment_t *dm_segs;',
    'size_t dm_mapsize; int _dm_flags; void *_dm_iommu; bus_dma_segment_t *dm_segs;')
  services = replace.call(services, '#include "drv-body.h"', "#include \"drv-body.h\"\nstatic unsigned service_calls, fail_service;\nstatic uint64_t allocation_alias;\nstatic bool split_maps;\nstatic unsigned bad_load_call, load_fault;\nstatic bool service_failure(void) { return ++service_calls == fail_service; }\n")
  services = replace.call(services, 'fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#c); exit(1);',
    'fprintf(stderr,"CHECK line %d: %s\\n",__LINE__,#c); abort();')
  services = replace.call(services, 'void *memory = malloc(n);', 'void *memory = aligned_alloc(PAGE_SIZE, n);')
  services = replace.call(services, '0x20000000 + i * BCMV3D_BO_MAX + j * chunk * 2',
    '(allocation_alias ? allocation_alias : UINT64_C(0x20000000) + i * UINT64_C(0x4000000)) + j * chunk * 2')
  services = replace.call(services, 'CHECK(f & BUS_DMA_PREFETCHABLE);',
    'CHECK(f == BUS_DMA_WAITOK || f == (BUS_DMA_WAITOK | BUS_DMA_PREFETCHABLE));')
  ['if (fail == 2 ||', 'if (fail == 3)', 'if (fail == 4)', 'if (fail == 5)'].each do |anchor|
    services = replace.call(services, anchor, "if (service_failure()) return EIO;\n\t" + anchor)
  end
  services = replace.call(services, 'm->dm_mapsize = n;', <<'C')
	m->dm_mapsize = n;
	if (split_maps && m->maxsegs > 1) {
		unsigned int k = 0;
		for (unsigned int i = 0; i < 16; i++) {
			if (dma_segments[i].memory != p)
				continue;
			for (int j = 0; j < dma_counts[i]; j++) {
				for (size_t off = 0; off < dma_lists[i][j].ds_len; off += PAGE_SIZE) {
					CHECK(k < (unsigned int)m->maxsegs);
					m->dm_segs[k] = dma_lists[i][j];
					m->dm_segs[k].ds_addr += off;
					m->dm_segs[k].ds_len = PAGE_SIZE;
					k++;
				}
			}
		}
		m->dm_nsegs = (int)k;
	}
	if (service_calls == bad_load_call) {
		if (load_fault == 1) m->_dm_flags = _BUS_DMAMAP_IS_BOUNCING;
		if (load_fault == 2) m->_dm_iommu = m;
		if (load_fault == 3) m->dm_segs[0].ds_addr += 4096;
	}
C
  services = replace.call(services, 'allocated++;', "allocated++;\n\t\tallocation_alias = 0;")
  write.call('bo-services.h', services)
  fixture = File.join(source, 'ember/tools/bcmv3d-gem-scanout-contract.c')
  read.call('ember/tools/bcmv3d-gem-scanout-contract.c')
  cc = Shellwords.split(ENV.fetch('CC', 'cc')); flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
  executor = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
  pages = ENV.fetch('SCANOUT_PAGES', '4096 65536').split
  compile = lambda do |name, page|
    out = work + '/contract-' + page
    run.call(name + '-compile-' + page, cc + %w[-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter] + flags +
      ['-I' + work, '-DPAGE_SIZE=' + page, '-MD', '-MF', work + '/' + name + '-' + page + '.d', fixture, '-o', out])
    out
  end
  pages.each do |page|
    out = compile.call('positive', page)
    unless ENV['SCANOUT_COMPILE_ONLY'] == '1'
      o, = run.call('positive-' + page, executor + [out]); print o
      unless ENV['SCANOUT_SKIP_HARNESS'] == '1'
        %w[segv abort hang check].each do |control|
          _, diagnostic, status, timed_out = run.call('harness-' + control + '-' + page,
            executor + [out], false, {'SCANOUT_HARNESS_CONTROL' => control}, control == 'hang' ? 1 : timeout_seconds)
          abort "bad harness classification #{control}" unless
            fatal_check.call(diagnostic, status, timed_out) == (control == 'check')
          abort 'hang escaped bound' if control == 'hang' && !timed_out
        end
      end
    end
  end
  unless ENV['SCANOUT_COMPILE_ONLY'] == '1' || ENV['SCANOUT_SKIP_MUTANTS'] == '1'
    mutations = {
      'bo-reference' => ['bo-body.h', 'bcmv3d_bo_reference', 'drm_gem_object_get(obj);', '(void)obj;'],
      'bo-functions' => ['bo-body.h', 'bcmv3d_bo_reference', 'obj->funcs != &bcmv3d_bo_funcs', 'false'],
      'bo-device' => ['bo-body.h', 'bcmv3d_bo_reference', 'obj->dev != sc->drm', 'false'],
      'bounce' => ['backing-body.h', 'mmu4_gem_map', '(map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0', 'false'],
      'iommu' => ['backing-body.h', 'mmu4_gem_map', 'map->_dm_iommu != NULL', 'false'],
      'map-size' => ['backing-body.h', 'mmu4_gem_map', 'map->dm_mapsize != size', 'false'],
      'parent-pa' => ['backing-body.h', 'mmu4_gem_pages', 'first != parent->dm_segs[pseg].ds_addr + poff', 'false'],
      'consumer-pa' => ['backing-body.h', 'mmu4_gem_pages', 'first != g->map->dm_segs[cseg].ds_addr + coff', 'false'],
      'last-byte' => ['backing-body.h', 'mmu4_gem_pages', 'va + offset + 4095,', 'va + offset + 4094,'],
      'native-stride' => ['backing-body.h', 'mmu4_gem_pages', 'offset += BCM2712_MMU4_PAGE_BYTES', 'offset += PAGE_SIZE'],
      'table-alias' => ['backing-body.h', 'mmu4_gem_aliases', 'mmu4_gem_allocation_overlap(a, &b->control[j], size)', 'false'],
      'top-index' => ['backing-body.h', 'bcmv3d_mmu4_backing_stage_gem', '(slot + 1) * 8, g->image.top', 'slot * 8, g->image.top'],
      'second-l2' => ['backing-body.h', 'bcmv3d_mmu4_backing_stage_gem', 'g->image.l2[i],', 'g->image.l2[0],'],
      'padding' => ['backing-body.h', 'bcmv3d_mmu4_backing_stage_gem', 'memset(g->control[i].kva, 0, b->status.control_bytes);', 'memset(g->control[i].kva, 0, 4096);'],
      'discard-top' => ['backing-body.h', 'bcmv3d_mmu4_backing_discard_staged_gem', 'memset(b->candidate_top + (slot + 1) * 8, 0, 8);', '/* missing top-pair discard */'],
      'discard-reference' => ['backing-body.h', 'mmu4_gem_free', 'drm_gem_object_put_unlocked(&g->bo->cma.base);', '(void)g->bo;'],
      'range-slot' => ['table-body.h', 'bcm2712_mmu4_encode_range', 'slot >= BCM2712_MMU4_SCANOUT_SLOTS', '(slot > BCM2712_MMU4_SCANOUT_SLOTS)'],
      'range-last-leaf' => ['table-body.h', 'bcm2712_mmu4_encode_range', 'input->frame_pa[i] == control[j]', 'i != 2024 && input->frame_pa[i] == control[j]']
    }
    ENV.fetch('SCANOUT_MUTANTS', mutations.keys.join(' ')).split.each do |name|
      path, function, before, after = mutations.fetch(name)
      original = File.read(work + '/' + path)
      first = original.index("\n#{function}("); abort "missing function #{function}" unless first
      last = original.index("\n}\n", first); abort "missing end #{function}" unless last
      body = original[first..last + 2]
      abort "non-unique mutant #{name}" unless body.scan(before).size == 1
      changed = original.dup
      changed[first, body.size] = body.sub(before) { after }
      write.call(path, changed)
      rejected = false
      pages.each do |page|
        out = compile.call(name, page)
        _, diagnostic, status, timed_out = run.call(name + '-' + page, executor + [out], false, {'SCANOUT_SHORT' => '1'})
        if !status.success?
          abort "non-causal mutant #{name}: #{diagnostic}" unless fatal_check.call(diagnostic, status, timed_out)
          rejected = true
        end
      end
      write.call(path, original)
      abort "mutant survived #{name}" unless rejected
      puts "PASS #{name}: compiled then CHECK/SIGABRT rejected"
    end
  end
ensure
  write.call('inputs.json', JSON.pretty_generate(inputs) + "\n")
  write.call('commands.json', JSON.pretty_generate(commands) + "\n")
  if ENV['SCANOUT_EVIDENCE']
    dst = ENV.fetch('SCANOUT_EVIDENCE'); abort 'evidence exists' if File.exist?(dst)
    FileUtils.cp_r(work, dst)
  end
  FileUtils.remove_entry(work) unless ENV['SCANOUT_KEEP_WORK'] == '1'
end
