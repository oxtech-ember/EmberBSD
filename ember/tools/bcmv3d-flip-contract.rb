#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json';require 'digest';require 'fileutils';require 'open3';require 'shellwords';require 'tmpdir'
src=File.realpath(ARGV[0]||File.expand_path('../..',__dir__))
d=ENV['FLIP_EVIDENCE'];abort 'new evidence required' if d&&File.exist?(d)
d||=Dir.mktmpdir('v3d-flip-',ENV.fetch('TMPDIR','/tmp'));FileUtils.mkdir_p(d)
inputs={};commands=[]
read=->(p){s=File.binread(src+'/'+p);inputs[p]=Digest::SHA256.hexdigest(s);s}
strip=->(s){s.lines.reject{|l|l.start_with?('#include ')}.join}
begin
 types=strip.call(read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_flip.h'))
 body=strip.call(read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_flip.c'))
 File.write(d+'/flip-actual.c',types+body)
 %w[c rb].each{|e|read.call('ember/tools/bcmv3d-flip-contract.'+e)}
 argv=Shellwords.split(ENV.fetch('CC','cc'))+%w[-std=c11 -Wall -Wextra -Werror]+Shellwords.split(ENV.fetch('CFLAGS',''))+['-I'+d,src+'/ember/tools/bcmv3d-flip-contract.c','-o',d+'/contract']
 run=lambda do |name,args,success=true|
  out='';err='';status=nil;timeout=false
  Open3.popen3(*args,pgroup:true) do |i,o,e,w|
   i.close;readers=[Thread.new{out=o.read},Thread.new{err=e.read}]
   limit=name.end_with?('compile') ? 180 : 30
   unless w.join(limit);timeout=true;begin Process.kill('KILL',-w.pid);rescue Errno::ESRCH;end;end
   status=w.value;readers.each(&:join)
  end
  File.write(d+'/'+name+'.stdout',out);File.write(d+'/'+name+'.stderr',err)
  commands<<{name:name,argv:args,exit:status.exitstatus,signal:status.termsig,timeout:timeout}
  abort name+': '+err if success&&(timeout||!status.success?)
  [out,err,status,timeout]
 end
 run.call('compile',argv)
 FileUtils.cp(d+'/contract',ENV['FLIP_OUTPUT']) if ENV['FLIP_OUTPUT']
 unless ENV['FLIP_COMPILE_ONLY']=='1'
  prefix=Shellwords.split(ENV.fetch('TEST_RUNNER',''))
  o,=run.call('positive',prefix+[d+'/contract']);print o
  unless ENV['FLIP_SKIP_MUTANTS']=='1'
   controls={
    'retire-after-frame'=>['bcmv3d_flip_observe','s->frame_epoch >= s->slot[i].retire_after','true'],
    'actual-latch'=>['bcmv3d_flip_observe','e->active_list == pending_head','true'],
    'same-active-lifetime'=>['bcmv3d_flip_submit','if ((int)slot != s->current)','if (true)'],
    'stale-callback'=>['bcmv3d_flip_observe','if (e->sequence <= s->sequence)','if (false)'],
    'sticky-fault'=>['bcmv3d_flip_fail','if (s->fault == 0)','if (true)'],
    'copy-owner'=>['flip_check','if (s->owner != s)','if (false)'],
    'sequence-gap'=>['bcmv3d_flip_observe','gap > 31','gap > 63'],
    'half-range'=>['bcmv3d_flip_observe','delta >= 32','delta > 32'],
    'claim-only-retired'=>['bcmv3d_flip_claim_retired','s->slot[slot].state != BCMV3D_FLIP_RETIRED','s->slot[slot].state != BCMV3D_FLIP_RETIRING'],
    'unknown-head'=>['bcmv3d_flip_observe','if (e->active_list != current_head && e->active_list != pending_head)','if ((void)current_head, false)'],
    'no-slot-reuse'=>['bcmv3d_flip_ready','if (s->slot[slot].state != BCMV3D_FLIP_FREE)','if (false)'],
    'blank-reuse'=>['bcmv3d_flip_reclaimed','slot == BCMV3D_FLIP_BLANK ?','false ?']
   }
   controls.each do |name,(fn,before,after)|
    first=body.index("\n#{fn}(");abort 'function boundary '+name unless first
    last=body.index("\n}\n",first);part=body[first..last+2];abort 'unique control '+name unless part.scan(before).size==1
    changed=body.dup;changed[first,part.size]=part.sub(before){after};File.write(d+'/flip-actual.c',types+changed)
    run.call(name+'-compile',argv);_,err,status,to=run.call(name,prefix+[d+'/contract'],false)
    abort 'noncausal control '+name unless !to&&status.exited?&&status.exitstatus==1&&err.lines.any?{|v|v.start_with?('CHECK ')}
    puts 'PASS compiled CHECK control '+name
   end
   File.write(d+'/flip-actual.c',types+body)
  end
 end
ensure
 File.write(d+'/inputs.json',JSON.pretty_generate(inputs)+"\n");File.write(d+'/commands.json',JSON.pretty_generate(commands)+"\n")
 FileUtils.remove_entry(d) unless ENV['FLIP_EVIDENCE']
end
