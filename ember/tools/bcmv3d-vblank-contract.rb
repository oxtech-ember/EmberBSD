#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json';require 'digest';require 'fileutils';require 'open3';require 'shellwords';require 'tmpdir'
src=File.realpath(ARGV[0]||File.expand_path('../..',__dir__))
d=ENV['VBLANK_EVIDENCE'];abort 'new evidence required' if d&&File.exist?(d)
d||=Dir.mktmpdir('v3d-vblank-',ENV.fetch('TMPDIR','/tmp'));FileUtils.mkdir_p(d)
inputs={};commands=[]
read=->(p){s=File.binread(src+'/'+p);inputs[p]=Digest::SHA256.hexdigest(s);s}
strip=->(s){s.lines.reject{|l|l.start_with?('#include ')}.join}
begin
 File.write(d+'/vblank-types.h',strip.call(read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_vblank.h')))
 body=strip.call(read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_vblank.c'));File.write(d+'/vblank-body.h',body)
 %w[c rb].each{|e|read.call('ember/tools/bcmv3d-vblank-contract.'+e)}
 argv=Shellwords.split(ENV.fetch('CC','cc'))+%w[-std=c11 -Wall -Wextra -Werror]+Shellwords.split(ENV.fetch('CFLAGS',''))+['-I'+d,src+'/ember/tools/bcmv3d-vblank-contract.c','-o',d+'/contract']
 run=lambda do |name,args,success=true|
  out='';err='';status=nil;timeout=false
  Open3.popen3(*args,pgroup:true) do |i,o,e,w|
   i.close;readers=[Thread.new{out=o.read},Thread.new{err=e.read}]
   unless w.join(30);timeout=true;begin Process.kill('KILL',-w.pid);rescue Errno::ESRCH;end;end
   status=w.value;readers.each(&:join)
  end
  File.write(d+'/'+name+'.stdout',out);File.write(d+'/'+name+'.stderr',err)
  commands<<{name:name,argv:args,exit:status.exitstatus,signal:status.termsig,timeout:timeout}
  abort name+': '+err if success&&(timeout||!status.success?)
  [out,err,status,timeout]
 end
 run.call('compile',argv)
 FileUtils.cp(d+'/contract',ENV['VBLANK_OUTPUT']) if ENV['VBLANK_OUTPUT']
 unless ENV['VBLANK_COMPILE_ONLY']=='1'
  prefix=Shellwords.split(ENV.fetch('TEST_RUNNER',''))
  o,=run.call('positive',prefix+[d+'/contract']);print o
  unless ENV['VBLANK_SKIP_MUTANTS']=='1'
   controls={
    'source-present'=>['bcmv3d_vblank_irq','if ((pending & PV_VFP_START) == 0)','if (false)'],
    'W1C-owned-only'=>['bcmv3d_vblank_irq','vblank_write(s, PV_INTSTAT, PV_VFP_START)','vblank_write(s, PV_INTSTAT, PV_VFP_START | 1U)'],
    'readback'=>['vblank_expect','return (value & mask) == expected ? 0 : vblank_fault(s, error);','return ((void)value, (void)mask, (void)expected, (void)error, 0);'],
    'copy-owner'=>['bcmv3d_vblank_arm','if (s->owner != s)','if (false)'],
    'write-barrier'=>['vblank_write','s->io->barrier(s->arg, BCMV3D_VBLANK_PV0);','(void)s;'],
    'mask-inherited'=>['bcmv3d_vblank_prepare','error = vblank_write(s, PV_INTEN, 0);','error = 0;']
   }
   controls.each do |name,(fn,before,after)|
    first=body.index("\n#{fn}(");abort 'function boundary '+name unless first
    last=body.index("\n}\n",first);part=body[first..last+2];abort 'unique control '+name unless part.scan(before).size==1
    changed=body.dup;changed[first,part.size]=part.sub(before){after};File.write(d+'/vblank-body.h',changed)
    run.call(name+'-compile',argv);_,err,status,to=run.call(name,prefix+[d+'/contract'],false)
    abort 'noncausal control '+name unless !to&&status.exited?&&status.exitstatus==1&&err.lines.any?{|v|v.start_with?('CHECK: ')}
    puts 'PASS compiled CHECK control '+name
   end
   File.write(d+'/vblank-body.h',body)
  end
 end
ensure
 File.write(d+'/inputs.json',JSON.pretty_generate(inputs)+"\n");File.write(d+'/commands.json',JSON.pretty_generate(commands)+"\n")
 FileUtils.remove_entry(d) unless ENV['VBLANK_EVIDENCE']
end
