#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json';require 'digest';require 'fileutils';require 'open3';require 'shellwords';require 'tmpdir'
src=File.realpath(ARGV[0]||File.expand_path('../..',__dir__))+'/'
d=ENV['SCANOUT_EVIDENCE'];abort 'new evidence required' if d&&File.exist?(d)
d||=Dir.mktmpdir('v3d-scanout-',ENV.fetch('TMPDIR','/tmp'));FileUtils.mkdir_p(d)
inputs={};commands=[];read=->(p){v=File.binread(src+p);inputs[p]=Digest::SHA256.hexdigest(v);v};strip=->(v){v.lines.reject{|x|x.start_with?('#include ')}.join}
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
 %w[c rb].each{|e|read.call('ember/tools/bcmv3d-scanout-contract.'+e)}
 args=Shellwords.split(ENV.fetch('CC','cc'))+%w[-std=c11 -Wall -Wextra -Werror]+Shellwords.split(ENV.fetch('CFLAGS',''))+['-I'+d,src+'ember/tools/bcmv3d-scanout-contract.c','-o',d+'/contract']
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
  o,=run.call('positive',exec);print o
  unless ENV['SCANOUT_SKIP_MUTANTS']=='1'
   controls={
    'prepare-wait-unlocked'=>['bcmv3d_scanout_prepare','error = bcmv3d_read_begin(&f->read, obj, 5 * hz);','mutex_enter(&n->process_lock); error = bcmv3d_read_begin(&f->read, obj, 5 * hz);'],
    'DMA-map-before-fence'=>['scanout_work','error = bcmv3d_mmu4_native_retire_gem(n->sc->mmu4_native, i, f);','error = ((void)bcmv3d_mmu4_native_retire_gem, bcmv3d_read_end(&f->read));'],
    'same-BO-lease'=>['bcmv3d_scanout_prepare','f->object == obj && !f->preparing &&','false && f->object == obj && !f->preparing &&'],
    'callback-outside-lock'=>['scanout_vblank','notify(notify_arg, snapshot->sequence, update.latched, error);','{ mutex_enter(&n->lock); notify(notify_arg, snapshot->sequence, update.latched, error); }'],
    'exposure-before-write'=>['bcmv3d_scanout_submit','error = bcmv3d_mmu4_native_expose_gem(n->sc->mmu4_native,\n\t\t    slot, f->object);','error = ((void)bcmv3d_mmu4_native_expose_gem, 0);'],
    'exact-frame-proof'=>['bcmv3d_scanout_get','f != &f->owner->frame[f->slot]','false'],
    'retire-fault-retains'=>['scanout_work','if (error == 0 && n->flip.fault != 0)','if (false)'],
    'DMA-before-prepare'=>['bcmv3d_scanout_prepare','error = bcmv3d_mmu4_native_prepare_gem_read(n->sc->mmu4_native,\n\t    f->slot, obj);','error = ((void)bcmv3d_mmu4_native_prepare_gem_read, 0);'],
    'sticky-write-fault'=>['scanout_write','return scanout_fault(n, EIO);','return 0;'],
    'DRM-reference'=>['bcmv3d_scanout_create','drm_dev_get(sc->drm);','(void)drm_dev_get;'],
    'pre-MMIO-fault'=>['bcmv3d_scanout_submit','if (n->flip.fault != 0) {','if (false) {'],
    'captured-sequence'=>['bcmv3d_scanout_submit','slot, captured.sequence);','slot, n->flip.sequence);'],
    'bind-baseline'=>['bcmv3d_scanout_create','event = error != 0 ? initial :','event = true ? initial :'],
    'completion-commit'=>['scanout_work','f->completion_committed = true;','f->completion_committed = false;'],
    'PV0-bind'=>['bcmv3d_scanout_create','error = bcmv3d_vblank_native_bind_snapshot(sc->vblank_native,
	    scanout_vblank, n, &snapshot);','error = ((void)bcmv3d_vblank_native_bind_snapshot, (void)scanout_vblank, 0);']
   }
   original=File.read(d+'/scanout-body.h')
   controls.each do |name,(fn,before,after)|
    before=before.gsub('\\n',"\n").gsub('\\t',"\t")
    first=original.index("\n#{fn}(");abort 'function boundary '+name unless first
    last=original.index("\n}\n",first);part=original[first..last+2]
    abort 'unique source control '+name unless part.scan(before).size==1
    changed=original.dup;changed[first,part.size]=part.sub(before){after}
    File.write(d+'/scanout-body.h',changed);run.call(name+'-compile',args)
    _,err,status,to=run.call(name,exec,false)
    abort 'noncausal native control '+name unless !to&&status.exited?&&status.exitstatus==1&&err.lines.any?{|x|x.start_with?('CHECK ')}
    puts 'PASS compiled CHECK control '+name
   end
   File.write(d+'/scanout-body.h',original)
  end
 end
ensure
 File.write(d+'/inputs.json',JSON.pretty_generate(inputs)+"\n");File.write(d+'/commands.json',JSON.pretty_generate(commands)+"\n")
 FileUtils.remove_entry(d) unless ENV['SCANOUT_EVIDENCE']
end
