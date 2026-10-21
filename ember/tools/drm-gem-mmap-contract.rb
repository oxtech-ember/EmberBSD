#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile actual strict, generic and legacy GEM mmap bodies with host services.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'optparse'
require 'shellwords'
require 'timeout'
options = {source: File.expand_path('../..', __dir__), cc: ENV.fetch('CC', 'cc'), cflags: ENV.fetch('CFLAGS', '')}
OptionParser.new do |p|
  p.on('--source PATH') { |v| options[:source] = File.expand_path(v) }
  p.on('--work PATH') { |v| options[:work] = File.expand_path(v) }
  p.on('--cc CC') { |v| options[:cc] = v }
  p.on('--cflags FLAGS') { |v| options[:cflags] = v }
end.parse!
raise '--work PATH is required' unless options[:work]
s = options[:source]; w = options[:work]
raise 'work already exists' if File.exist?(w)
FileUtils.mkdir_p(w)
files = {vm: 'sys/external/bsd/drm2/drm/drm_gem_vm.c', vma: 'sys/external/bsd/drm2/drm/drm_vma_manager.c',
         driver: 'sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.c', header: 'sys/external/bsd/drm2/dist/include/drm/drm_gem.h',
         cdev: 'sys/external/bsd/drm2/drm/drm_cdevsw.c', errno: 'sys/sys/errno.h',
         fixture: 'ember/tools/drm-gem-mmap-contract.c', runner: 'ember/tools/drm-gem-mmap-contract.rb'}
pins = -> { files.to_h { |k,p| [k, Digest::SHA256.file(s + '/' + p).hexdigest] } }
before = pins.call
sources = files.to_h { |k,p| [k, File.read(s + '/' + p)] }
def extract(text, name)
  patterns = [/^(?:static )?(?:int|void)\n#{name}\(.*?^}\n/m,
              /^struct drm_vma_offset_node \*\n#{name}\(.*?^}\n/m]
  matches = patterns.flat_map { |p| text.scan(p) }
  raise "function boundary changed: #{name}" unless matches.size == 1
  matches.first
end
vma_names = %w[drm_vma_offset_lock_lookup drm_vma_offset_unlock_lookup drm_vma_offset_lookup_locked drm_vma_offset_exact_lookup_locked drm_vma_offset_exact_lookup]
vm_names = %w[drm_gem_mmap_object_locked drm_gem_mmap_object drm_gem_or_legacy_mmap_object drm_gem_mmap_object_strict drm_gem_pager_reference drm_gem_pager_detach]
body = vma_names.map { |n| extract(sources[:vma], n) }.join("\n") + vm_names.map { |n| extract(sources[:vm], n) }.join("\n") + extract(sources[:cdev], 'drm_fop_mmap')
bindings = sources[:driver].scan(/^\s*\.mmap_object = .*,$/)
raise 'native binding boundary changed' unless bindings.size == 1
raise 'strict prototype missing' unless sources[:header].include?("int drm_gem_mmap_object_strict(struct drm_device *, off_t, size_t, int,\n    struct uvm_object **, voff_t *, struct file *);")
elast = sources[:errno][/^#define\s+ELAST\s+(\d+)/,1] or raise 'target ELAST missing'
erestart = sources[:errno][/^#define\s+ERESTART\s+(-?\d+)/,1] or raise 'target ERESTART missing'
File.write(w+'/target-errno.h', "#undef ERESTARTSYS\n#define ERESTARTSYS #{elast.to_i+1}\n#undef ERESTART\n#define ERESTART #{erestart}\n")
File.write(w+'/gem-mmap-body.h',body)
File.write(w+'/gem-mmap-binding.h',bindings.first+"\n")
commands = []
run = lambda do |argv, expected, marker|
  output = error = nil; status = nil
  Open3.popen3(*argv, pgroup: true) do |input,out,err,waiter|
    input.close; stdout=Thread.new {out.read}; stderr=Thread.new {err.read}
    begin
      Timeout.timeout(60) { output=stdout.value; error=stderr.value; status=waiter.value }
    rescue Timeout::Error
      begin; Process.kill('KILL',-waiter.pid); rescue Errno::ESRCH; end
      waiter.value
      raise 'process timed out'
    ensure
      stdout.join; stderr.join
    end
  end
  index = format('%03d', commands.length)
  File.write(w+'/'+index+'.stdout', output)
  File.write(w+'/'+index+'.stderr', error)
  commands << {argv:argv,exit:status.exitstatus,signal:status.termsig,stdout:index+'.stdout',stderr:index+'.stderr'}
  File.write(w+'/commands.json',JSON.pretty_generate(commands)+"\n")
  raise "unexpected result #{argv.shelljoin}: #{status.inspect}\n#{output}#{error}" unless status.exited? && status.exitstatus==expected
  raise 'sanitizer diagnostic' if (output+error).match?(/AddressSanitizer:|UndefinedBehaviorSanitizer:|LeakSanitizer:|runtime error:/)
  raise "missing causal marker #{marker}" unless (output+error).include?(marker)
  output+error
end
checks=0
# The extracted cdevsw body retains its unused kernel flagsp parameter.
compile = lambda do |page|
  argv=[*Shellwords.split(options[:cc]),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',*Shellwords.split(options[:cflags]),"-DTEST_PAGE_SHIFT=#{page}",'-I'+w,s+'/'+files[:fixture],'-o',w+'/contract']
  run.call(argv,0,''); argv
end
%w[12 16].each do |page|
  compile.call(page)
  File.write(w+"/page#{page}.log",run.call([w+'/contract'],0,'actual GEM mmap checks'))
  checks+=1
end
controls = {
  'old-binding' => [nil,nil,'binding','drm_gem_mmap_object_strict','drm_gem_mmap_object','uobj == &object.gemo_uvmobj && relative == 0'],
  'exact-size' => ['drm_vma_offset_lookup_locked(dev->vma_offset_manager, startpage,','drm_vma_offset_exact_lookup_locked(dev->vma_offset_manager, startpage,','body',nil,nil,'error == expected'],
  'interior' => ['node != NULL && drm_vma_node_start(node) == startpage','node != NULL','body',nil,nil,'error == expected'],
  'ownership' => ['if (!drm_vma_node_is_allowed(node, fp->f_data))','if (false && !drm_vma_node_is_allowed(node, fp->f_data))','body',nil,nil,'error == expected'],
  'denied-ref' => ["drm_gem_object_put_unlocked(obj);\n\t\treturn -EACCES;","return -EACCES;",'body',nil,nil,'object.refcount.count == refs'],
  'zero-ref' => ['if (!kref_get_unless_zero(&obj->refcount))','if ((drm_gem_object_get(obj), false))','body',nil,nil,'obj->refcount.count != 0'],
  'no-retain' => ['if (!kref_get_unless_zero(&obj->refcount))','if (false)','body',nil,nil,'object.refcount.count == refs + 1'],
  'zero-length' => ['nbytes == 0 ||','false ||','body',nil,nil,'error == expected']
}
controls.each do |name,(old,replacement,where,bind_old,bind_new,marker)|
  if where=='binding'
    File.write(w+'/gem-mmap-binding.h',bindings.first.sub(bind_old,bind_new)+"\n")
  else
    raise 'control boundary '+name unless body.scan(Regexp.new(Regexp.escape(old))).size==1
    mutant = body.sub(old,replacement)
    if name == 'zero-length'
      # The overflow guard independently rejects size_t zero minus one.
      boundary = 'if ((uintmax_t)(nbytes - 1) >'
      raise 'zero-length overflow boundary' unless mutant.scan(Regexp.new(Regexp.escape(boundary))).size == 1
      mutant = mutant.sub(boundary, 'if (nbytes != 0 && (uintmax_t)(nbytes - 1) >')
    end
    File.write(w+'/gem-mmap-body.h',mutant)
  end
  compile.call('12')
  File.write(w+'/control-'+name+'.log',run.call([w+'/contract'],1,marker))
  File.write(w+'/gem-mmap-body.h',body);File.write(w+'/gem-mmap-binding.h',bindings.first+"\n")
  checks+=1
end
raise 'inputs changed during contract' unless before==pins.call
File.write(w+'/receipt.json',JSON.pretty_generate({checks:checks,source_pins:before,scope:'Actual VMA/GEM/cdevsw bodies, exact native binding, controlled locks/refs and legacy services. No physical mmap, GPU or cache qualification.'})+"\n")
puts "PASS: #{checks} actual GEM mmap contexts and causal controls"
