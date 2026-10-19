#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual mechanism and reject causative mutations at CHECK sites.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'digest'
require 'json'
source=File.realpath(ARGV[0] || File.expand_path('../..',__dir__))
evidence=ENV['QUIESCE_EVIDENCE']
abort 'new evidence required' if evidence && File.exist?(evidence)
FileUtils.mkdir_p(evidence) if evidence
paths=%w[sys/arch/arm/broadcom/bcm2712_display_quiesce.c sys/arch/arm/broadcom/bcm2712_display_quiesce.h ember/tools/bcm2712-display-quiesce-contract.c ember/tools/bcm2712-quiesce-fixture.h ember/tools/bcm2712-display-quiesce-contract.rb]
inputs=paths.to_h{|p|[p,File.binread(source+'/'+p)]}
commands=[]
Dir.mktmpdir('display-quiesce-',ENV.fetch('TMPDIR','/tmp')) do |work|
  puts 'display quiesce temporary directory: '+work
  inputs.each{|p,t|File.write(work+'/'+File.basename(p),t)}
  run=lambda do |name,argv|
    out,err,status=Open3.capture3(*argv)
    commands << {name:name,argv:argv,status:status.exitstatus,signal:status.termsig}
    if evidence
      File.write(evidence+'/'+name+'.stdout',out);File.write(evidence+'/'+name+'.stderr',err)
      File.write(evidence+'/commands.json',JSON.pretty_generate(commands)+"\n")
    end
    [out,err,status]
  end
  compile=lambda do |name|
    argv=[*Shellwords.split(ENV.fetch('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror',*Shellwords.split(ENV.fetch('CFLAGS','')),'-I'+work,work+'/bcm2712-display-quiesce-contract.c',work+'/bcm2712_display_quiesce.c','-o',work+'/contract']
    out,err,s=run.call('compile-'+name,argv); abort(out+err) unless s.success?
  end
  runner=ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
  compile.call('positive');out,err,s=run.call('positive',runner+[work+'/contract']);abort(out+err) unless s.success?;print out
  controls={
    'version-first'=>['READ(BCM2712_QUIESCE_HVS, 0, s->version);','READ(BCM2712_QUIESCE_HVS, 0x20, s->version);','r == 0 && o == 0'],
    'second-pv'=>['((b->pv_control[1] | b->pv_video[1]) & PV_EN) != 0','false','== EBUSY'],
    'other-channels'=>['((b->control[1] | b->control[2]) & HVS_ENB) != 0','false','== EBUSY'],
    'other-modes'=>['((b->status[1] | b->status[2]) & HVS_MODE) != 0','false','== EBUSY'],
    'pv-clock'=>['(b->pv_control[0] & PV_CLOCK) != 0','false','== EBUSY'],
    'settle'=>['ops->delay(arg, 20000);','ops->delay(arg, 10);','qb.microseconds >= 20000'],
    'fresh-rmw'=>['READ(BCM2712_QUIESCE_HVS, ctrl, value);\n\tWRITE(BCM2712_QUIESCE_HVS, ctrl, value & ~HVS_ENB);','WRITE(BCM2712_QUIESCE_HVS, ctrl, value & ~HVS_ENB);','(v & 0x44000000U) == 0x44000000U'],
    'reset-clear'=>['(s->control & HVS_ENB) == 0 && (s->status & HVS_MODE) == 0)','(s->control & (HVS_ENB | HVS_RESET)) == 0 && (s->status & HVS_MODE) == 0)','== 0'],
    'poll-bound'=>['#define POLL_STEPS\t100','#define POLL_STEPS\t99','qb.microseconds =='],
    'io-after-fault'=>['if (error != 0) goto done;','if (false) goto done;','== EIO'],
    'write-fault'=>['error = ops->write(arg, (r), (o), (v));','error = ops->write(arg, (r), (o), (v)); error = 0;','!qb.faulted && qb.barrier_pending'],
    'barrier'=>['ops->barrier(arg, (r));','if (false) ops->barrier(arg, (r));','!qb.faulted && !qb.barrier_pending'],
    'sticky'=>['if (s->attempted || s->complete || s->error != 0)','if (false)','qb.events == n']
  }
  text=inputs.fetch(paths[0])
  unless ENV['SKIP_MUTANTS']=='1'
    controls.each do |name,(before,after,check)|
      before=before.gsub('\n',"\n").gsub('\t',"\t"); after=after.gsub('\n',"\n").gsub('\t',"\t")
      abort 'anchor absent '+name unless text.include?(before)
      # Require RESET to clear only in the post-write poll, not inactive fast path.
      changed = name == 'reset-clear' ? text.sub('if ((s->control & HVS_ENB) == 0 && (s->status & HVS_MODE) == 0)', 'if ((s->control & (HVS_ENB | HVS_RESET)) == 0 && (s->status & HVS_MODE) == 0)') : text.sub(before,after)
      File.write(work+'/bcm2712_display_quiesce.c',changed);compile.call(name)
      out,err,s=run.call('control-'+name,runner+[work+'/contract'])
      abort 'control did not reach CHECK '+name+"\n"+err unless !s.success? && err.include?('quiesce CHECK') && err.include?(check)
      puts 'display quiesce control '+name+': CHECK'
    end
  end
  if evidence
    FileUtils.mkdir_p(evidence+'/inputs');inputs.each{|p,t|File.write(evidence+'/inputs/'+File.basename(p),t)}
    File.write(evidence+'/source.json',JSON.pretty_generate(inputs.transform_values{|t|Digest::SHA256.hexdigest(t)})+"\n")
  end
end
