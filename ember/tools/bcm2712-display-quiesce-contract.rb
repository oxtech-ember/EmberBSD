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
    'third-channel'=>['(b->control[2] & HVS_ENB) != 0','false','== EBUSY'],
    'third-mode'=>['(b->status[2] & HVS_MODE) != 0','false','== EBUSY'],
    'pv-clock'=>['(s->pipe[channel].pv_control & PV_CLOCK) != 0','false','== EBUSY'],
    'both-video'=>['/* Stop both video feeds before assuming either HVS-to-PV routing. */\n\tfor (channel = 0; channel < 2; channel++)', '/* Stop both video feeds before assuming either HVS-to-PV routing. */\n\tfor (channel = 0; channel < 1; channel++)','(qb.regs[1][1] & 1) == 0 && (qb.regs[2][1] & 1) == 0'],
    'both-control'=>['/* Both PVs are disabled before the first channel reset. */','/* Both PVs are disabled before the first channel reset. */','CONTROL_SCOPE'],
    'settle'=>['ops->delay(arg, 20000);','ops->delay(arg, 10);','qb.microseconds >= 20000'],
    'settle-before-last'=>['settle = true;', 'if (channel == 0) ops->delay(arg, 20000); settle = false;', 'qb.microseconds >= 20000 && qb.settled > qb.last_video'],
    'fresh-rmw'=>['/* Read again after RESET; preserve unrelated hardware changes. */\n\t\t\tREAD(BCM2712_QUIESCE_HVS, ctrl + channel * stride, value);','/* Missing fresh read. */','last.kind == 0 && last.region == r && last.offset == o'],
    'reset-clear'=>['(p->control & HVS_ENB) == 0 &&','(p->control & (HVS_ENB | HVS_RESET)) == 0 &&','== 0'],
    'second-hvs'=>['if (!p->hvs_disabled) {','if (channel == 0 && !p->hvs_disabled) {','qb.writes =='],
    'pipe-error'=>['s->pipe[channel].error = error;','s->pipe[channel].error = 0;','s.pipe[port].error == e'],
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
      changed = text.sub(before,after)
      if name == 'both-control'
        scope=text[/\tif \(settle\).*?(?=\t\/\* Both PVs)/m] || abort('control phase scope absent')
        changed=text.sub(scope,scope.sub('channel < 2','channel < 1'))
        check='((qb.regs[1][0] | qb.regs[1][1] | qb.regs[2][0] | qb.regs[2][1]) & 1) == 0'
      end

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
