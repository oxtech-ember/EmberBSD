#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json'
require 'digest'
require 'fileutils'
require 'tmpdir'
require 'shellwords'
require 'open3'
src=File.realpath(ARGV[0]||File.expand_path('../..',__dir__))
d=ENV['GEM_FB_EVIDENCE'];abort 'new evidence required' if d&&File.exist?(d)
d||=Dir.mktmpdir('gem-fb-',ENV.fetch('TMPDIR','/tmp'));FileUtils.mkdir_p(d)
inputs={};commands=[];Process.setrlimit(Process::RLIMIT_CORE,0,0)
read=->(p){b=File.binread(src+'/'+p);inputs[p]=Digest::SHA256.hexdigest(b);b}
run=lambda do |name,argv,required=true|
 o,e,s=Open3.capture3(*argv)
 File.write(d+'/'+name+'.stdout',o);File.write(d+'/'+name+'.stderr',e)
 commands<<{name:name,argv:argv,exit:s.exitstatus,signal:s.termsig}
 abort name+': '+e if required&&!s.success?
 [o,e,s]
end
begin
 p='sys/external/bsd/drm2/drm/drm_gem_framebuffer_helper.c'
 body=read.call(p).lines.reject{|l|l.start_with?('#include ','__KERNEL_RCSID(')}.join
 File.write(d+'/fb-body.h',body)
 slab=read.call('sys/external/bsd/common/include/linux/slab.h')
 struct=slab[/^struct linux_malloc \{.*?^} __aligned\(ALIGNBYTES \+ 1\);/m] or abort 'slab structure'
 funcs=%w[kmalloc kzalloc kfree].map do |n|
  a=slab.scan(/^static inline (?:void \*|void)\n#{n}\(.*?^}/m)
  abort 'slab boundary '+n unless a.size==1
  a.first
 end
 File.write(d+'/slab-body.h',slab.split(/^#ifndef /,2).first+struct+"\n"+funcs.join("\n"))
 fill=read.call('sys/external/bsd/drm2/dist/drm/drm_modeset_helper.c')
 a=fill.scan(/^void drm_helper_mode_fill_fb_struct\(.*?^}/m);abort 'fill boundary' unless a.size==1
 File.write(d+'/fill-body.h',fill.split(/^#include /,2).first+a.first)
 %w[c rb].each{|e|read.call('ember/tools/drm-gem-fb-contract.'+e)}
 argv=Shellwords.split(ENV.fetch('CC','cc'))+%w[-std=c11 -Wall -Wextra -Werror -Wno-unused-function]+Shellwords.split(ENV.fetch('CFLAGS',''))+['-I'+d,src+'/ember/tools/drm-gem-fb-contract.c','-o',d+'/contract']
 run.call('compile',argv)
 FileUtils.cp(d+'/contract',ENV['GEM_FB_OUTPUT']) if ENV['GEM_FB_OUTPUT']
 unless ENV['GEM_FB_COMPILE_ONLY']=='1'
  executor=Shellwords.split(ENV.fetch('TEST_RUNNER',''))
  out,=run.call('positive',executor+[d+'/contract']);print out
  unless ENV['GEM_FB_SKIP_MUTANTS']=='1'
   controls={
    'zeroed-storage'=>['fb = kzalloc(sizeof(*fb), GFP_KERNEL);','fb = kmalloc(sizeof(*fb), GFP_KERNEL);','fb->obj[i] == NULL'],
    'matching-free'=>['fail1:\tkfree(fb);', 'fail1:\tkmem_free(fb, sizeof(*fb));','p == allocation && size == allocation_size']
   }
   controls.each do |name,(before,after,expected)|
    before=before.gsub('\\t',"\t");after=after.gsub('\\t',"\t")
    abort 'nonunique control '+name unless body.scan(before).size==1
    File.write(d+'/fb-body.h',body.sub(before,after));run.call(name+'-compile',argv)
    _,err,status=run.call(name,executor+[d+'/contract'],false)
    found=err.lines.grep(/^CHECK \d+: /).map{|l|l.sub(/^CHECK \d+: /,'').strip}
    abort 'noncausal control '+name+': '+err unless status.exited?&&status.exitstatus==1&&found==[expected]
    puts 'PASS compiled named control '+name
   end
   File.write(d+'/fb-body.h',body)
  end
 end
ensure
 File.write(d+'/inputs.json',JSON.pretty_generate(inputs)+"\n")
 File.write(d+'/commands.json',JSON.pretty_generate(commands)+"\n")
 FileUtils.remove_entry(d) unless ENV['GEM_FB_EVIDENCE']
end
