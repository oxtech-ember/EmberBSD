#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json';require 'digest';require 'fileutils';require 'open3';require 'shellwords';require 'tmpdir'
src=File.realpath(ARGV[0]||File.expand_path('../..',__dir__))+'/'
d=ENV['SCANOUT_EVIDENCE'];abort 'new evidence required' if d&&File.exist?(d)
d||=Dir.mktmpdir('v3d-scanout-',ENV.fetch('TMPDIR','/tmp'));FileUtils.mkdir_p(d)
inputs={};commands=[];reports=[];paths={};original=nil;read=->(p){v=File.binread(src+p);paths[p]=src+p;inputs[p]=Digest::SHA256.hexdigest(v);v};strip=->(v){v.lines.reject{|x|x.start_with?('#include ')}.join}
begin
 {
  'flip-types.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_flip.h',
  'flip-body.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_flip.c',
  'read-types.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_read.h',
  'pv-types.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_vblank_native.h',
  'scanout-types.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_scanout.h',
  'scanout-body.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_scanout.c',
  'hvs-types.h'=>'sys/arch/arm/broadcom/bcm2712_display_hvsvar.h',
  'hvs-body.h'=>'sys/arch/arm/broadcom/bcm2712_display_hvs.c',
  'display-types.h'=>'sys/arch/arm/broadcom/bcm2712_display.h'
 }.each{|out,p|File.write(d+'/'+out,strip.call(read.call(p)))}
 errno=read.call('sys/sys/errno.h');linux_errno=read.call('sys/external/bsd/common/include/linux/errno.h')
 definitions=%w[ELAST ETIME].map do |name|
  values=errno.scan(/^#define\s+#{name}\s+(\d+)\b/)
  abort 'unique target errno required: '+name unless values.size==1
  "#undef #{name}\n#define #{name} #{values.first.first}\n"
 end.join
 abort 'unique target ERESTARTSYS required' unless linux_errno.scan(/^#define\s+ERESTARTSYS\s+\(ELAST\s*\+\s*1\)/).size==1
 File.write(d+'/target-errno.h',definitions+"#undef ERESTARTSYS\n#define ERESTARTSYS (ELAST+1)\n")
 %w[c rb].each do |e|
  key='ember/tools/bcmv3d-scanout-contract.'+e;paths[key]=File.join(__dir__,File.basename(key))
  inputs[key]=Digest::SHA256.file(paths[key]).hexdigest
 end
 original=File.read(d+'/scanout-body.h')
 args=Shellwords.split(ENV.fetch('CC','cc'))+%w[-std=c11 -Wall -Wextra -Werror]+Shellwords.split(ENV.fetch('CFLAGS',''))+['-I'+d,File.join(__dir__,'bcmv3d-scanout-contract.c'),'-o',d+'/contract']
 run=lambda do |name,argv,required=true|
  o='';e='';st=nil;timeout=false
  Open3.popen3(*argv,pgroup:true) do |i,out,err,w|
   i.close;readers=[Thread.new{o=out.read},Thread.new{e=err.read}]
   limit=name.end_with?('compile') ? 180 : 30
   unless w.join(limit);timeout=true;begin Process.kill('KILL',-w.pid);rescue Errno::ESRCH;end;end
   st=w.value;readers.each(&:join)
  end
  File.write(d+'/'+name+'.stdout',o);File.write(d+'/'+name+'.stderr',e)
  commands<<{name:name,argv:argv,exit:st.exitstatus,signal:st.termsig,timeout:timeout}
  abort name+': '+e if required&&(timeout||!st.success?)
  [o,e,st,timeout]
 end
 run.call('compile',args)
 FileUtils.cp(d+'/contract',ENV['SCANOUT_OUTPUT']) if ENV['SCANOUT_OUTPUT']
 unless ENV['SCANOUT_COMPILE_ONLY']=='1'
  exec=Shellwords.split(ENV.fetch('TEST_RUNNER',''))+[d+'/contract']
  o,=run.call('positive',exec);reports<<o
  unless ENV['SCANOUT_SKIP_MUTANTS']=='1'
   controls={
    'retired-wait-unlocked'=>['bcmv3d_scanout_wait_retired','waited = dma_fence_wait_timeout(fence, true, timeout);','mutex_enter(&n->process_lock); waited = dma_fence_wait_timeout(fence, true, timeout);'],
    'retired-wait-ERESTARTSYS'=>['bcmv3d_scanout_wait_retired','error = EINTR;','error = ERESTARTSYS;'],
    'retired-wait-errno-bound'=>['bcmv3d_scanout_wait_retired','waited >= -ELAST','true'],
    'retired-status-errno-bound'=>['bcmv3d_scanout_wait_retired','status >= -ELAST','true'],
    'retired-fence-status'=>['bcmv3d_scanout_wait_retired','status = dma_fence_get_status(fence);','status = ((void)dma_fence_get_status, 1);'],
    'retired-fence-retain'=>['bcmv3d_scanout_wait_retired','fence = dma_fence_get(f->read.fence);','fence = ((void)dma_fence_get, f->read.fence);'],
    'retired-worker-proof'=>['bcmv3d_scanout_wait_retired','!f->completion_committed ||','false ||'],
    'retired-map-proof'=>['bcmv3d_scanout_wait_retired','f->staged || f->published || f->exposed ||','false ||'],
    'retired-read-proof'=>['bcmv3d_scanout_wait_retired','f->read.owner != NULL || f->read.object != NULL || f->read.fence != NULL ||\n\t    f->read.prior != NULL || f->read.poisoned','false'],
    'retired-FREE-recheck'=>['bcmv3d_scanout_wait_retired','n->flip.slot[f->slot].state != BCMV3D_FLIP_FREE','false'],
    'retired-fault-recheck'=>['bcmv3d_scanout_wait_retired','if (n->flip.fault != 0)','if (false)'],
    'retired-locked-recheck'=>['bcmv3d_scanout_wait_retired','mutex_enter(&n->process_lock);','(void)mutex_enter;'],'reserve-no-PREWRITE'=>['bcmv3d_scanout_reserve','f->staged = true;','f->staged = true; (void)bcmv3d_mmu4_native_prepare_gem_read(n->sc->mmu4_native, f->slot, obj);'],
    'cancel-never-exposed-unmap'=>['scanout_cancel_unexposed','error = bcmv3d_mmu4_native_unmap_gem(n->sc->mmu4_native, f->slot);','error = ((void)bcmv3d_mmu4_native_unmap_gem, 0);'],
    'finalize-sticky-fault'=>['bcmv3d_scanout_finalize','if (n->flip.fault != 0)','if (false)'],
    'prepare-wait-unlocked'=>['bcmv3d_scanout_finalize','error = bcmv3d_read_wait(&f->read, timeout);','mutex_enter(&n->process_lock); error = bcmv3d_read_wait(&f->read, timeout);'],
    'DMA-map-before-fence'=>['scanout_work','error = bcmv3d_mmu4_native_retire_gem(n->sc->mmu4_native, i, f);','error = ((void)bcmv3d_mmu4_native_retire_gem, bcmv3d_read_end(&f->read));'],
    'same-BO-lease'=>['bcmv3d_scanout_reserve','if (f->object != obj)','if (true)'],
    'callback-outside-lock'=>['scanout_vblank','notify(notify_arg, snapshot->sequence, update.latched, error);','{ mutex_enter(&n->lock); notify(notify_arg, snapshot->sequence, update.latched, error); }'],
    'exposure-before-write'=>['bcmv3d_scanout_submit','error = bcmv3d_mmu4_native_expose_gem(n->sc->mmu4_native,\n\t\t    slot, f->object);','error = ((void)bcmv3d_mmu4_native_expose_gem, 0);'],
    'exact-frame-proof'=>['bcmv3d_scanout_get','f != &f->owner->frame[f->slot]','false'],
    'retire-fault-retains'=>['scanout_work','if (error == 0 && n->flip.fault != 0)','if (false)'],
    'DMA-before-prepare'=>['bcmv3d_scanout_finalize','error = bcmv3d_mmu4_native_prepare_gem_read(n->sc->mmu4_native,\n\t    f->slot, f->object);','error = ((void)bcmv3d_mmu4_native_prepare_gem_read, 0);'],
    'sticky-write-fault'=>['scanout_write','return scanout_fault(n, EIO);','return 0;'],
    'DRM-reference'=>['bcmv3d_scanout_create','drm_dev_get(sc->drm);','(void)drm_dev_get;'],
    'pre-MMIO-fault'=>['bcmv3d_scanout_submit','if (n->flip.fault != 0) {','if (false) {'],
    'captured-sequence'=>['bcmv3d_scanout_submit','slot, captured.sequence);','slot, n->flip.sequence);'],
    'bind-baseline'=>['bcmv3d_scanout_create','event = error != 0 ? initial :','event = true ? initial :'],
    'completion-commit'=>['scanout_work','f->completion_committed = true;','f->completion_committed = false;'],
    'PV0-bind'=>['bcmv3d_scanout_create','error = bcmv3d_vblank_native_bind_snapshot(sc->vblank_native,
	    scanout_vblank, n, &snapshot);','error = ((void)bcmv3d_vblank_native_bind_snapshot, (void)scanout_vblank, 0);']
   }
   controls.each do |name,(fn,before,after)|
    before=before.gsub('\\n',"\n").gsub('\\t',"\t")
    first=original.index("\n#{fn}(");abort 'function boundary '+name unless first
    last=original.index("\n}\n",first);part=original[first..last+2]
    abort 'unique source control '+name unless part.scan(before).size==1
    changed=original.dup;changed[first,part.size]=part.sub(before){after}
    File.write(d+'/scanout-body.h',changed);run.call(name+'-compile',args)
    _,err,status,to=run.call(name,exec,false)
    abort 'noncausal native control '+name unless !to&&status.exited?&&status.exitstatus==1&&err.lines.any?{|x|x.start_with?('CHECK ')}
    reports<<'PASS compiled CHECK control '+name+"\n"
   end
   File.write(d+'/scanout-body.h',original)
  end
 end
 verified=paths.to_h{|key,path|[key,Digest::SHA256.file(path).hexdigest]}
 File.write(d+'/verified-inputs.json',JSON.pretty_generate(verified)+"\n")
 abort 'source inputs changed during the contract run' unless verified==inputs
 reports.each{|report|print report}
ensure
 File.write(d+'/scanout-body.h',original) if original
 File.write(d+'/inputs.json',JSON.pretty_generate(inputs)+"\n");File.write(d+'/commands.json',JSON.pretty_generate(commands)+"\n")
 FileUtils.remove_entry(d) unless ENV['SCANOUT_EVIDENCE']
end
