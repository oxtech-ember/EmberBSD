#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json';require 'digest';require 'fileutils';require 'open3';require 'shellwords';require 'tmpdir'
src=File.realpath(ARGV[0]||File.expand_path('../..',__dir__))+'/'
d=ENV['VBLANK_NATIVE_EVIDENCE'];abort 'new evidence required' if d&&File.exist?(d)
d||=Dir.mktmpdir('v3d-vblank-native-',ENV.fetch('TMPDIR','/tmp'));FileUtils.mkdir_p(d)
inputs={};commands=[];read=->(p){v=File.binread(src+p);inputs[p]=Digest::SHA256.hexdigest(v);v};strip=->(v){v.lines.reject{|x|x.start_with?('#include ')}.join}
begin
 {
  'vblank-types.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_vblank.h',
  'vblank-body.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_vblank.c',
  'native-types.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_vblank_native.h',
  'native-body.h'=>'sys/external/bsd/drm2/bcmv3d/bcmv3d_vblank_native.c',
  'display-types.h'=>'sys/arch/arm/broadcom/bcm2712_display.h'
 }.each{|out,p|File.write(d+'/'+out,strip.call(read.call(p)))}
 %w[ember/tools/bcmv3d-vblank-contract.c ember/tools/bcmv3d-vblank-native-contract.c ember/tools/bcmv3d-vblank-native-contract.rb].each{|p|read.call(p)}
 args=Shellwords.split(ENV.fetch('CC','cc'))+%w[-std=c11 -Wall -Wextra -Werror]+Shellwords.split(ENV.fetch('CFLAGS',''))+['-I'+d,'-I'+src+'ember/tools',src+'ember/tools/bcmv3d-vblank-native-contract.c','-o',d+'/contract']
 run=lambda do |name,argv,required=true|
  o='';e='';st=nil;timeout=false
  Open3.popen3(*argv,pgroup:true) do |i,out,err,w|
   i.close;readers=[Thread.new{o=out.read},Thread.new{e=err.read}]
   unless w.join(30);timeout=true;begin Process.kill('KILL',-w.pid);rescue Errno::ESRCH;end;end
   st=w.value;readers.each(&:join)
  end
  File.write(d+'/'+name+'.stdout',o);File.write(d+'/'+name+'.stderr',e)
  commands<<{name:name,argv:argv,exit:st.exitstatus,signal:st.termsig,timeout:timeout}
  abort name+': '+e if required&&(timeout||!st.success?)
  [o,e,st,timeout]
 end
 run.call('compile',args)
 FileUtils.cp(d+'/contract',ENV['VBLANK_NATIVE_OUTPUT']) if ENV['VBLANK_NATIVE_OUTPUT']
 unless ENV['VBLANK_NATIVE_COMPILE_ONLY']=='1'
  exec=Shellwords.split(ENV.fetch('TEST_RUNNER',''))+[d+'/contract']
  o,=run.call('positive',exec);print o
  unless ENV['VBLANK_NATIVE_SKIP_MUTANTS']=='1'
   controls={
    'registry'=>['vblank_native_create','if (sc->vblank_native != NULL)','if (false)'],
    'callback-lock'=>['vblank_native_intr',"if ((handled || error != 0) && notify != NULL)\n\t\tnotify(notify_arg, &snapshot);","if ((handled || error != 0) && notify != NULL) { mutex_enter(&n->lock); notify(notify_arg, &snapshot); }"],
    'before-handle'=>['vblank_native_intr','if (!n->core.armed || n->fault != 0)','if (false)'],
    'parent-mask'=>['vblank_native_fault','if (n->ih != NULL && !n->masked)','if (!n->masked)'],
    'bind-once'=>['vblank_native_bind','n->notify != NULL ? EBUSY : 0;','false ? EBUSY : 0;'],
    'real-handle'=>['vblank_native_create','n->ih = ih;','n->ih = NULL;'],
    'IRQ-read-onfault'=>['vblank_native_read','if (cpu_intr_p() || cpu_softintr_p())','if (false)'],
    'IRQ-write-onfault'=>['vblank_native_write','if (cpu_intr_p() || cpu_softintr_p())','if (false)'],
    'native-barrier'=>['vblank_native_barrier',"bus_space_barrier(n->bst, n->pv, 0, 0x100,\n\t    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);",'(void)bus_space_barrier;'],
    'prepare-first'=>['vblank_native_create','error = bcmv3d_vblank_prepare(&n->core, &io, n, variant);','error = ((void)io, (void)variant, 0);'],
    'DRM-reference'=>['vblank_native_create','drm_dev_get(sc->drm);','(void)drm_dev_get;']
   }
   original=File.read(d+'/native-body.h')
   controls.each do |name,(fn,before,after)|
    fn='bcmv3d_'+fn if fn.start_with?('vblank_native_')&&%w[create bind].any?{|v|fn.end_with?(v)}
    first=original.index("\n#{fn}(");abort 'function boundary '+name unless first
    last=original.index("\n}\n",first);part=original[first..last+2]
    abort 'unique source control '+name unless part.scan(before).size==1
    changed=original.dup;changed[first,part.size]=part.sub(before){after}
    File.write(d+'/native-body.h',changed);run.call(name+'-compile',args)
    _,err,status,to=run.call(name,exec,false)
    abort 'noncausal native control '+name unless !to&&status.exited?&&status.exitstatus==1&&err.lines.any?{|x|x.start_with?('CHECK: ')}
    puts 'PASS compiled CHECK control '+name
   end
   File.write(d+'/native-body.h',original)
  end
 end
ensure
 File.write(d+'/inputs.json',JSON.pretty_generate(inputs)+"\n");File.write(d+'/commands.json',JSON.pretty_generate(commands)+"\n")
 FileUtils.remove_entry(d) unless ENV['VBLANK_NATIVE_EVIDENCE']
end
