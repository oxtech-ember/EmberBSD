#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile actual native offset callers and core publication with pthread services.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'optparse'
require 'shellwords'
require 'timeout'
options = { source: File.expand_path('../..', __dir__), cc: ENV.fetch('CC', 'cc'), cflags: ENV.fetch('CFLAGS', '') }
OptionParser.new do |p|
  p.on('--source PATH') { |v| options[:source] = File.expand_path(v) }
  p.on('--work PATH') { |v| options[:work] = File.expand_path(v) }
end.parse!
s = options[:source]; w = options[:work]
raise '--work NEW_DIRECTORY is required' unless w && !File.exist?(w)
FileUtils.mkdir_p(w)
files = { bo: 'sys/external/bsd/drm2/bcmv3d/bcmv3d_bo.c', driver: 'sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.c',
          header: 'sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.h', vma: 'sys/external/bsd/drm2/drm/drm_vma_manager.c',
          gem: 'sys/external/bsd/drm2/dist/drm/drm_gem.c', dumb: 'sys/external/bsd/drm2/dist/drm/drm_dumb_buffers.c',
          fixture: 'ember/tools/bcmv3d-mmap-offset-contract.c', runner: 'ember/tools/bcmv3d-mmap-offset-contract.rb' }
pin = -> { files.to_h { |key, path| [key, Digest::SHA256.file(s+'/'+path).hexdigest] } }
before = pin.call
sources = files.to_h { |key, path| [key, File.read(s+'/'+path)] }
def extract(text, name)
  matches = text.scan(/^(?:static )?int(?:\n| )#{name}\(.*?^}\n/m) + text.scan(/^void\n#{name}\(.*?^}\n/m)
  raise 'function boundary '+name unless matches.size == 1
  matches.first
end
parts = {}
{ vma: %w[drm_vma_offset_add drm_vma_offset_remove], gem: %w[drm_gem_create_mmap_offset_size drm_gem_create_mmap_offset drm_gem_dumb_map_offset],
  bo: %w[bcmv3d_bo_mmap_offset bcmv3d_dumb_map_offset bcmv3d_mmap_bo], dumb: %w[drm_mode_mmap_dumb_ioctl] }.each do |key, names|
  names.each { |name| parts[name] = extract(sources[key], name) }
end
binding = sources[:driver].scan(/^\s*\.dumb_map_offset = .*,$/)
raise 'native dumb binding missing or ambiguous' unless binding.size == 1
raise 'native prototype missing' unless sources[:header].include?("int bcmv3d_dumb_map_offset(struct drm_file *, struct drm_device *,\n    uint32_t, uint64_t *);")
write = lambda do |functions = parts, line = binding.first|
  File.write(w+'/offset-body.h', functions.values.join("\n"))
  File.write(w+'/offset-binding.h', line+"\n")
end
write.call
commands = []
run = lambda do |name, argv, expected, marker|
  output = error = nil; status = nil
  Open3.popen3(*argv, pgroup: true) do |input, out, err, waiter|
    input.close
    readers = [Thread.new { out.read }, Thread.new { err.read }]
    begin
      Timeout.timeout(60) { output = readers[0].value; error = readers[1].value; status = waiter.value }
    rescue Timeout::Error
      begin; Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
      waiter.value
      raise 'process timed out: '+name
    ensure
      readers.each(&:join)
    end
  end
  File.write(w+'/'+name+'.stdout', output); File.write(w+'/'+name+'.stderr', error)
  commands << { name: name, argv: argv, exit: status.exitstatus, signal: status.termsig }
  File.write(w+'/commands.json', JSON.pretty_generate(commands)+"\n")
  raise name+': unexpected result '+status.inspect+"\n"+output+error unless status.exited? && status.exitstatus == expected
  raise name+': sanitizer diagnostic' if (output+error).match?(/AddressSanitizer:|UndefinedBehaviorSanitizer:|LeakSanitizer:|runtime error:/)
  raise name+': causal marker missing' unless (output+error).include?(marker)
end
compile = [*Shellwords.split(options[:cc]), '-std=c11', '-pthread', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
           *Shellwords.split(options[:cflags]), '-I'+w, s+'/'+files[:fixture], '-o', w+'/contract']
run.call('positive-compile', compile, 0, '')
run.call('positive', [w+'/contract'], 0, 'actual native mmap offset checks')
controls = {
  'both-unlocked' => ['bcmv3d_bo_mmap_offset', "\tmutex_lock(&dev->struct_mutex);\n", '', 'published && manager.vom_nodes.node == node'],
  'duplicate-first' => ['bcmv3d_bo_mmap_offset', "\tmutex_lock(&dev->struct_mutex);\n", '', 'allocations == 1'],
  'v3d-bypass' => ['bcmv3d_mmap_bo', 'error = bcmv3d_bo_mmap_offset(dev, obj, &args->offset);',
                   "error = drm_gem_create_mmap_offset(obj);\n\tif (error == 0) args->offset = drm_vma_node_offset_addr(&obj->vma_node);", 'published && manager.vom_nodes.node == node'],
  'dumb-bypass' => ['bcmv3d_dumb_map_offset', 'error = bcmv3d_bo_mmap_offset(dev, obj, offset);',
                    "error = drm_gem_dumb_map_offset(file, dev, handle, offset);", 'published && manager.vom_nodes.node == node'],
  'dumb-binding' => [nil, nil, nil, 'published && manager.vom_nodes.node == node'],
  'error-output' => ['bcmv3d_bo_mmap_offset', 'if (error == 0)', 'if (true)', 'published && manager.vom_nodes.node == node'],
  'v3d-ref-leak' => ['bcmv3d_mmap_bo', 'drm_gem_object_put_unlocked(obj);', '(void)obj;', 'atomic_load(&object.refs) == 2 && published']
}
controls.each do |name, (function, from, to, marker)|
  mutant = parts.dup; line = binding.first
  if function
    text = mutant.fetch(function)
    raise 'control boundary '+name unless text.scan(from).size == 1
    text = text.sub(from, to)
    # Remove the paired unlock for the deliberately unprotected historical path.
    text = text.sub("\tmutex_unlock(&dev->struct_mutex);\n", '') if name == 'both-unlocked' || name == 'duplicate-first'
    mutant[function] = text
  else
    line = '\t.dumb_map_offset = NULL,'.gsub('\\t', "\t")
  end
  if name == 'duplicate-first'
    fixture = File.read(s+'/'+files[:fixture]).sub('for (unsigned stage = 0; stage < 2; stage++)', 'for (unsigned stage = 0; stage < 1; stage++)').sub('concurrent(1 - stage, a, b, alias != 0);', 'concurrent(stage, a, b, alias != 0);')
    File.write(w+'/control-fixture.c', fixture)
  end
  write.call(mutant, line)
  argv = compile.dup
  argv[argv.index(s+'/'+files[:fixture])] = w+'/control-fixture.c' if name == 'duplicate-first'
  run.call(name+'-compile', argv, 0, '')
  run.call(name, [w+'/contract'], 1, marker)
  write.call
end
raise 'source inputs changed' unless before == pin.call
File.write(w+'/receipt.json', JSON.pretty_generate({source_pins: before, concurrent_cases: 16, compiled_controls: controls.size,
  scope: 'Actual native V3D and core MODE_MAP_DUMB dispatch, shared helper and actual VMA add/remove. Controlled host allocator/ref/locks; no target, VM or display acceptance.'})+"\n")
puts "PASS: 16 concurrent cases and #{controls.size} compiled causal controls"
