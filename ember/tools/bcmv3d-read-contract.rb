#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json';require 'digest';require 'fileutils';require 'tmpdir';require 'shellwords';require 'open3'
src=File.realpath(ARGV[0]||File.expand_path('../..',__dir__))
d=ENV['READ_EVIDENCE'];abort 'new evidence required' if d&&File.exist?(d)
d||=Dir.mktmpdir('v3d-read-',ENV.fetch('TMPDIR','/tmp'));FileUtils.mkdir_p(d)
inputs={};commands=[]
read=->(p){v=File.binread(src+'/'+p);inputs[p]=Digest::SHA256.hexdigest(v);v}
strip=->(s){s.lines.reject{|l|l.start_with?('#include ')}.join}
begin
 %w[c h].each{|e|File.write(d+(e=='h'?'/read-types.h':'/read-body.h'),strip.call(read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_read.'+e)))}
 %w[c h].each{|e|File.write(d+(e=='h'?'/resv-types.h':'/resv-body.h'),strip.call(read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_resv.'+e)))}
 gem=read.call('sys/external/bsd/drm2/dist/drm/drm_gem.c')
 functions=%w[drm_gem_lock_reservations drm_gem_unlock_reservations].map do |n|
  a=gem.scan(/^(?:int|void)\n#{n}\(.*?^}/m);abort 'GEM boundary '+n unless a.size==1;a.first
 end
 File.write(d+'/gem-lock-body.h',gem.split(/^#include /,2).first+functions.join("\n"))
 text=read.call('sys/external/bsd/drm2/linux/linux_dma_resv.c')
 body=%w[dma_resv_add_shared_fence dma_resv_add_excl_fence].map{|n|a=text.scan(/^void\n#{n}\(.*?^}/m);abort 'fence function boundary '+n unless a.size==1;a.first}
 File.write(d+'/shared-body.h',text.split(/^#include /,2).first+body.join("\n"))
 %w[c rb].each{|e|read.call('ember/tools/bcmv3d-read-contract.'+e)}
 argv=Shellwords.split(ENV.fetch('CC','cc'))+%w[-std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter]+Shellwords.split(ENV.fetch('CFLAGS',''))+['-I'+d,src+'/ember/tools/bcmv3d-read-contract.c','-o',d+'/contract']
 run=lambda do |name,argv,required=true|
  out=err='';status=nil;timeout=false
  Open3.popen3(*argv,pgroup:true) do |i,o,e,w|
   i.close;readers=[Thread.new{out=o.read},Thread.new{err=e.read}]
   unless w.join(30)
    timeout=true
    begin Process.kill('KILL',-w.pid);rescue Errno::ESRCH;end
   end
   status=w.value;readers.each(&:join)
  end
  File.write(d+'/'+name+'.stdout',out);File.write(d+'/'+name+'.stderr',err)
  commands<<{name:name,argv:argv,exit:status.exitstatus,signal:status.termsig,timeout:timeout}
  abort name+': '+err if required&&(timeout||!status.success?)
  [out,err,status,timeout]
 end
 run.call('compile',argv)
 FileUtils.cp(d+'/contract',ENV['READ_OUTPUT']) if ENV['READ_OUTPUT']
 unless ENV['READ_COMPILE_ONLY']=='1'
  executor=Shellwords.split(ENV.fetch('TEST_RUNNER',''))
  out,=run.call('positive',executor+[d+'/contract']);print out
  unless ENV['READ_SKIP_MUTANTS']=='1'
   original=File.read(d+'/read-body.h')
   controls={
    'reserve-no-wait'=>['prior = dma_fence_get(dma_resv_get_excl(obj->resv));','prior = dma_fence_get(dma_resv_get_excl(obj->resv)); if (prior != NULL) (void)dma_fence_wait_timeout(prior, true, 17);',1,1,'waits==0 && producer.refs==3 && a.prior==&producer'],
    'unique-context'=>['dma_fence_context_alloc(1)','0',1,1,'reservation.fence->shared_count==2 && a.fence!=b.fence && a.fence->context!=b.fence->context'],
    'writer-wait'=>['waited = dma_fence_wait_timeout(prior, true, timeout);','waited = timeout;',1,1,'error==expected'],
    'writer-status'=>['error = ret > 0 ? 0 : ret == 0 ? EIO : -ret;','(void)ret; error = 0;',1,1,'error==expected'],
    'writer-timeout'=>['error = waited == 0 ? ETIME : -waited;','error = 0;',1,1,'error==expected'],
    'retain-GEM'=>['drm_gem_object_get(obj);','(void)obj;',1,1,'bcmv3d_read_end(&copy)==EPERM && object.refs==2 && signals==0'],
    'prior-reference'=>['prior = dma_fence_get(dma_resv_get_excl(obj->resv));','prior = dma_resv_get_excl(obj->resv);',1,1,'f->refs==(reservation.fence_excl==f ? 3U : 2U)'],
    'wait-copy-owner'=>['if (s->owner != s)','if (false)',1,3,'bcmv3d_read_wait(&copy,17)==EPERM'],
    'copy-owner'=>['if (s->owner != s)','if (false)',2,3,'bcmv3d_read_end(&copy)==EPERM && object.refs==2 && signals==0'],
    'poison-retention'=>['return ret < 0 ? -ret : ret == 0 ? 0 : EIO;','drm_gem_object_put_unlocked(s->object); return ret < 0 ? -ret : ret == 0 ? 0 : EIO;',1,1,'a.fence->status==0 && b.fence->status==-EIO && object.refs==3'],
    'completion-lock'=>['ret = dma_fence_signal(s->fence);','(void)dma_resv_lock_interruptible(s->object->resv, NULL); ret = dma_fence_signal(s->fence);',1,2,'r==&reservation && !r->locked'],
    'RCU-grace'=>['call_rcu(&fence->rcu, bcmv3d_read_free_rcu);','bcmv3d_read_free_rcu(&fence->rcu);',1,1,'rcu_running && f->refs==0 && f->initialized && f->lock->live'],
    'lock-destroy'=>['spin_lock_destroy(&read->lock);','(void)read;',1,1,'!allocations && !locks_live && !fences_live && !rcu_queue'],
    'failure-unlock'=>['dma_resv_unlock(obj->resv);','(void)obj;',1,2,'!reservation.locked && producer.refs==2'],
    'failure-fence-put'=>['dma_fence_put(&read->base);','(void)read;',1,1,'!allocations && !locks_live && !fences_live && !rcu_queue']
   }
   controls.each do |name,(before,after,nth,total,expected)|
    seen=0;mutated=original.gsub(before){|v|seen+=1;seen==nth ? after : v}
    abort 'control source boundary '+name unless seen==total
    File.write(d+'/read-body.h',mutated);run.call(name+'-compile',argv)
    _,err,status,timeout=run.call(name,executor+[d+'/contract'],false)
    found=err.lines.grep(/^CHECK \d+: /).map{|v|v.sub(/^CHECK \d+: /,'').strip}
    abort 'noncausal control '+name+': '+err unless !timeout&&status.exited?&&status.exitstatus==1&&found==[expected]
    puts 'PASS compiled named control '+name
   end
   File.write(d+'/read-body.h',original)
  end

 end
ensure
 File.write(d+'/inputs.json',JSON.pretty_generate(inputs)+"\n");File.write(d+'/commands.json',JSON.pretty_generate(commands)+"\n")
 FileUtils.remove_entry(d) unless ENV['READ_EVIDENCE']
end
