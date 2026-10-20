#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV.fetch(0, File.join(__dir__, '../..')))
work = Dir.mktmpdir('bcmv3d-mmu4-range-', ENV.fetch('TMPDIR', '/tmp'))
puts "MMU4 range temporary directory: #{work}"
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
timeout_seconds = Integer(ENV.fetch('RANGE_TIMEOUT', '60'))
abort 'RANGE_TIMEOUT must be positive' unless timeout_seconds.positive?
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
  !timed_out && status.signaled? && status.termsig == Signal.list.fetch('ABRT') && (diagnostic.include?('CHECK ') || diagnostic.include?('ICAC contract failed'))
end
begin
  run.call('native-export', ['ruby', source + '/ember/tools/bcmv3d-mmu4-native-contract.rb'], true,
    {'MMU4_NATIVE_COMPILE_ONLY'=>'1', 'MMU4_NATIVE_EVIDENCE'=>work+'/native-export'})
  run.call('gem-export', ['ruby', source + '/ember/tools/bcmv3d-gem-scanout-contract.rb'], true,
    {'SCANOUT_COMPILE_ONLY'=>'1', 'SCANOUT_PAGES'=>'4096', 'SCANOUT_EVIDENCE'=>work+'/gem-export'})
  Dir[work+'/native-export/*.h'].each { |p| FileUtils.cp(p, work) }
  Dir[work+'/gem-export/*.h'].each { |p| FileUtils.cp(p, work) }
  # Complete actual fixture bodies; only its fixed one-write native service
  # counter is generalized to count the repeated retained flushes.
  icac = File.read(work+'/icac-services.h')
  icac = replace.call(icac, 'CHECK(bcm2712_iommuc_owner->mmu4.fault == 0 && ++writes == 1);',
    "CHECK(bcm2712_iommuc_owner->mmu4.fault == 0 && ++writes >= 1);\n\trange_cache_written(writes);")
  write.call('icac-services.h', "void range_cache_written(unsigned int);\n" + icac)
  services = File.read(work+'/bo-services.h')
  %w[kmem_zalloc kmem_free bus_dmamem_alloc bus_dmamem_free bus_dmamem_map bus_dmamem_unmap bus_dmamap_create bus_dmamap_destroy bus_dmamap_load bus_dmamap_unload].each do |name|
    first=services.index(name+'('); abort 'service missing '+name unless first
    brace=services.index('{', first); services.insert(brace+1, ' CHECK(!range_icac_owned()); ')
  end
  write.call('bo-services.h', "int range_icac_owned(void);\n" + services)
  front = read.call('ember/tools/bcmv3d-gem-scanout-contract.c').split("\nstatic struct drm_device drm;").first
  front = replace.call(front, '#define atomic_store_relaxed(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)',
    "static void range_atomic_store(uint32_t *, uint32_t);\nstatic void range_dma_sync(bus_dmamap_t, size_t, int);\n#define atomic_store_relaxed(p, v) range_atomic_store((p), (v))")
  first=front.index("static void\nbus_dmamap_sync("); last=front.index("\n}\n", first)
  abort 'DMA sync service boundary' unless first && last
  front[first..last+2] = <<'C'
static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t size, int flags)
{
	(void)tag;
	CHECK(offset == 0);
	range_dma_sync(map, size, flags);
}
C
  write.call('range-bridge-services.h', front)
  %w[sys/arch/arm/broadcom/bcm2712_iommuc_executor.h sys/arch/arm/broadcom/bcm2712_iommuc_native.c sys/external/bsd/drm2/bcmv3d/bcmv3d_mmu4_native.c sys/external/bsd/drm2/bcmv3d/bcmv3d_mmu4_native.h sys/external/bsd/drm2/bcmv3d/bcmv3d_mmu4_backing.c sys/external/bsd/drm2/bcmv3d/bcmv3d_mmu4_backing.h ember/tools/bcmv3d-mmu4-range-contract.rb ember/tools/bcmv3d-mmu4-range-contract.c ember/tools/bcmv3d-mmu4-range-backing.c ember/tools/bcmv3d-mmu4-native-contract.c].each { |p| read.call(p) }
  cc=Shellwords.split(ENV.fetch('CC','cc'));flags=Shellwords.split(ENV.fetch('CFLAGS',''))
  executor=Shellwords.split(ENV.fetch('TEST_RUNNER',''))
  pages=ENV.fetch('RANGE_PAGES','4096 65536').split
  common=cc+%w[-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter]+flags
  acpica=ENV.fetch('ACPICA_INCLUDE',source+'/sys/external/bsd/acpica/dist/include')
  compile=lambda do |name,page|
    object=work+'/bridge-'+page+'.o';out=work+'/contract-'+page
    run.call(name+'-bridge-compile-'+page,common+['-DPAGE_SIZE='+page,'-I'+work,'-I'+source+'/ember/tools','-MD','-MF',work+'/'+name+'-bridge-'+page+'.d','-c',source+'/ember/tools/bcmv3d-mmu4-range-backing.c','-o',object])
    run.call(name+'-native-compile-'+page,common+['-DPAGE_SIZE='+page,'-I'+work,'-I'+acpica,'-I'+source+'/ember/tools','-MD','-MF',work+'/'+name+'-native-'+page+'.d',source+'/ember/tools/bcmv3d-mmu4-range-contract.c',object,'-o',out])
    out
  end
  pages.each do |page|
    out=compile.call('positive',page)
    next if ENV['RANGE_COMPILE_ONLY']=='1'
    o,=run.call('positive-'+page,executor+[out]);print o
    unless ENV['RANGE_SKIP_HARNESS']=='1'
      %w[segv abort hang check].each do |control|
        _,e,st,to=run.call('harness-'+control+'-'+page,executor+[out],false,{'RANGE_HARNESS_CONTROL'=>control},control=='hang' ? 1 : timeout_seconds)
        abort 'invalid harness classification '+control unless fatal_check.call(e,st,to)==(control=='check')
      end
    end
  end
  unless ENV['RANGE_COMPILE_ONLY']=='1' || ENV['RANGE_SKIP_MUTANTS']=='1'
    mutations={
      'retirement-proof'=>['backing-body.h','bcmv3d_mmu4_backing_authorize_retired','if (error == 0)', 'if (true)'],
      'retirement-latch'=>['backing-body.h','bcmv3d_mmu4_backing_authorize_retired','g->retired = true;', 'g->retired = false;'],
      'retirement-delegate'=>['adapter-body.h','mmu4_native_range','bcmv3d_mmu4_backing_authorize_retired(n->backing, slot, retired);','((void)bcmv3d_mmu4_backing_authorize_retired, 0);'],
      'consumer-prewrite'=>['backing-body.h','bcmv3d_mmu4_backing_prepare_gem_read','BUS_DMASYNC_PREWRITE);','BUS_DMASYNC_POSTWRITE);'],
      'consumer-map'=>['backing-body.h','bcmv3d_mmu4_backing_prepare_gem_read','b->sc->dmat, g->map, 0,','b->sc->dmat, g->bo->cma.dmamap, 0,'],
      'consumer-postwrite'=>['backing-body.h','mmu4_gem_postwrite','BUS_DMASYNC_POSTWRITE);','BUS_DMASYNC_PREWRITE);'],
      'consumer-identity'=>['backing-body.h','bcmv3d_mmu4_backing_prepare_gem_read','if (&g->bo->cma.base != obj)','if (false)'],
      'consumer-required'=>['backing-body.h','bcmv3d_mmu4_backing_expose_gem','if (!g->read_synced)','if (false)'],
      'exposure-latch'=>['backing-body.h','bcmv3d_mmu4_backing_expose_gem','g->exposed = true;','g->exposed = false;'],
      'exposure-unmap'=>['backing-body.h','bcmv3d_mmu4_backing_range_check','if (g->exposed && (!g->retired || op != BCMV3D_MMU4_RANGE_UNMAP))','if (false)'],
      'top-index'=>['backing-body.h','bcmv3d_mmu4_backing_range','(slot + 1) * 2 + i','slot * 2 + i'],
      'top-exposure'=>['backing-body.h','bcmv3d_mmu4_backing_range',"g->linked = true;\n\t\ts->mutation_attempted = true;","g->linked = true;\n\t\ts->mutation_attempted = false;"],
      'leaf-exposure'=>['backing-body.h','bcmv3d_mmu4_backing_range',"if (g->linked)\n\t\ts->mutation_attempted = true;","if (g->linked)\n\t\ts->mutation_attempted = false;"],
      'l2-prewrite'=>['backing-body.h','bcmv3d_mmu4_backing_range',"BUS_DMASYNC_PREWRITE);\n\t\ts->syncs++;\n\t}\n\tif (!g->linked)","BUS_DMASYNC_POSTWRITE);\n\t\ts->syncs++;\n\t}\n\tif (!g->linked)"],
      'top-prewrite'=>['backing-body.h','bcmv3d_mmu4_backing_range',"BUS_DMASYNC_PREWRITE);\n\t\ts->syncs++;\n\t}\n\terror = invalidate", "BUS_DMASYNC_POSTWRITE);\n\t\ts->syncs++;\n\t}\n\terror = invalidate"],
      'skip-invalidate'=>['backing-body.h','bcmv3d_mmu4_backing_range','error = invalidate(arg);','error = ((void)invalidate, (void)arg, 0);'],
      'resident-pool'=>['backing-body.h','bcmv3d_mmu4_backing_stage_gem',"if (old != NULL) {\n\t\t\tg->control[i] = old->control[i];","if (false) {\n\t\t\tg->control[i] = old->control[i];"],
      'local-word'=>['adapter-body.h','mmu4_native_range_invalidate','n->bsh, 0, 0x04090807U','n->bsh, 0, 0x04090803U'],
      'local-barrier'=>['adapter-body.h','mmu4_native_range_invalidate',"bus_space_barrier(n->bst, n->bsh, 0, MMU4_LENGTH,\n\t    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);", '(void)n->bst;'],
      'local-busy'=>['adapter-body.h','mmu4_native_range_invalidate','s->raw_control & 0x80U','s->raw_control & 0x8000U'],
      'local-timeout'=>['adapter-body.h','mmu4_native_range_invalidate','return ETIMEDOUT;','return 0;'],
      'shared-flush'=>['adapter-body.h','mmu4_native_range_invalidate',"error = bcm2712_iommuc_core_enable_flush(n->cache,\n\t    BCM2712_IOMMUC_MMU4);",'error = 0;'],
      'range-abort'=>['adapter-body.h','mmu4_native_range','bcmv3d_mmu4_backing_range_abort(n->backing, error);','(void)n->backing;'],
      'unmap-reclaim'=>['adapter-body.h','mmu4_native_range','error = bcmv3d_mmu4_backing_reclaim_unmapped(n->backing, slot);','error = 0;'],
      'retained-reservation'=>['native.h','bcm2712_iommuc_execute_mmu4_retained','sc->core.status.reserved != IOMMUC_MMU4_MEMBER','false'],
      'retained-retain'=>['native.h','bcm2712_iommuc_execute_mmu4_retained','sc->core.status.retained != IOMMUC_MMU4_MEMBER','false']
    }
    ENV.fetch('RANGE_MUTANTS',mutations.keys.join(' ')).split.each do |name|
      path,function,before,after=mutations.fetch(name);original=File.read(work+'/'+path)
      first=original.index("\n#{function}(");abort 'function boundary '+name unless first
      last=original.index("\n}\n",first);body=original[first..last+2]
      if name=='retained-reservation' || name=='retained-retain'
        abort 'expected two retained checks' unless body.scan(before).size==2
        changed=body.sub(before,after)
      else
        abort 'nonunique mutant '+name unless body.scan(before).size==1
        changed=body.sub(before){after}
      end
      mutated=original.dup;mutated[first,body.size]=changed;write.call(path,mutated)
      pages.each do |page|
        out=compile.call('mutant-'+name,page)
        _,e,st,to=run.call('mutant-'+name+'-'+page,executor+[out],false)
        abort 'noncausal compiled mutant '+name+': '+e unless fatal_check.call(e,st,to)
      end
      write.call(path,original);puts 'PASS compiled CHECK/SIGABRT mutant '+name
    end
  end
ensure
  write.call('inputs.json',JSON.pretty_generate(inputs)+"\n")
  write.call('commands.json',JSON.pretty_generate(commands)+"\n")
  if ENV['RANGE_EVIDENCE']
    dst=ENV.fetch('RANGE_EVIDENCE');abort 'evidence exists' if File.exist?(dst);FileUtils.cp_r(work,dst)
  end
  FileUtils.remove_entry(work)
end
