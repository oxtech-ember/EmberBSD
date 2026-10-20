#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['DUMB_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('v3d-dumb-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(dir)
inputs = {}; commands = []
read = lambda do |path|
  data = File.binread(source + '/' + path)
  inputs[path] = Digest::SHA256.hexdigest(data)
  data
end
strip = ->(body) { body.lines.reject { |line| line.start_with?('#include ', '__KERNEL_RCSID(') }.join }
begin
  { 'bo-body.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_bo.c',
    'drv-body.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.h',
    'uapi-body.h' => 'sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h'
  }.each { |name, path| File.write(dir + '/' + name, strip.call(read.call(path))) }
  mode = read.call('sys/external/bsd/drm2/dist/include/uapi/drm/drm_mode.h')
  argument = mode[/struct drm_mode_create_dumb \{.*?\n\};/m]
  abort 'Actual dumb UAPI boundary missing' unless argument
  File.write(dir + '/dumb-uapi-body.h', argument + "\n")
  %w[sys/external/bsd/drm2/bcmv3d/bcmv3d_dumb.h ember/tools/bcm2712-v3d-bo-contract.c
     ember/tools/bcmv3d-dumb-contract.c ember/tools/bcmv3d-dumb-contract.rb].each { |p| read.call(p) }
  run = lambda do |name, argv, required = true|
    out = ''; err = ''; status = nil; timeout = false
    Open3.popen3(*argv, pgroup: true) do |stdin, stdout, stderr, waiter|
      stdin.close
      readers = [Thread.new { out = stdout.read }, Thread.new { err = stderr.read }]
      unless waiter.join(name.end_with?('compile') ? 180 : 30)
        timeout = true
        begin; Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
      end
      status = waiter.value; readers.each(&:join)
    end
    File.write(dir + '/' + name + '.stdout', out); File.write(dir + '/' + name + '.stderr', err)
    commands << { name: name, argv: argv, exit: status.exitstatus, signal: status.termsig, timeout: timeout }
    abort "#{name}: #{err}" if required && (timeout || !status.success?)
    [out, err, status, timeout]
  end
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + dir,
    source + '/ember/tools/bcmv3d-dumb-contract.c', '-o', dir + '/contract']
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [dir + '/contract']
  run.call('compile', compile); out, = run.call('positive', execute); print out
  page64 = compile.dup.insert(1, '-DPAGE_SIZE=65536U'); page64[-1] = dir + '/contract-page64'
  run.call('page64-compile', page64)
  out, = run.call('page64', Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [page64[-1]]); print out
  unless ENV['DUMB_SKIP_MUTANTS'] == '1'
    controls = {
      'bpp' => ['args->bpp != 32 ||', 'false ||'],
      'flags' => ['args->flags != 0 ||', 'false ||'],
      'zero-output' => ['args->size = 0;', 'args->size = UINT64_MAX;'],
      'pitch64' => ['+ 63) & ~UINT64_C(63)', '+ 31) & ~UINT64_C(31)'],
      'maximum-inclusive' => ['size > BCMV3D_BO_MAX', 'size >= BCMV3D_BO_MAX'],
      'native-scatter' => ['(uint32_t)size, false);', '(uint32_t)size, true);'],
      'rounded-size' => ['args->size = bo->cma.base.size;', 'args->size = size;'],
      'constructor-put' => ['drm_gem_object_put_unlocked(&bo->cma.base);', '(void)bo;'],
      'allocator-errno' => ['return PTR_ERR(bo);', 'return -PTR_ERR(bo);'],
      'handle-errno' => ['return error;', 'return -error;']
    }
    original = File.read(dir + '/bo-body.h')
    first = original.index("\nbcmv3d_dumb_create("); last = original.index("\n}\n", first)
    body = original[first..last + 2]
    controls.each do |name, (before, after)|
      abort 'Unique callback control required: ' + name unless body.scan(before).size == 1
      changed = original.dup; changed[first, body.size] = body.sub(before) { after }
      File.write(dir + '/bo-body.h', changed); run.call(name + '-compile', compile)
      _, err, status, timeout = run.call(name, execute, false)
      abort 'Noncausal callback control: ' + name unless !timeout && status.exited? &&
        status.exitstatus == 1 && err.lines.any? { |line| line.start_with?('FAIL line ') }
      puts 'PASS compiled FAIL control ' + name
    end
    File.write(dir + '/bo-body.h', original)
  end
ensure
  File.write(dir + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(dir + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(dir) unless evidence
end
