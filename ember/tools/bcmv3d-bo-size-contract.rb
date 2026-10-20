#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['BO_SIZE_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('v3d-bo-size-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(dir)
inputs = {}; commands = []; bodies = {}
read = lambda do |path|
  text = File.binread(source + '/' + path)
  inputs[path] = Digest::SHA256.hexdigest(text)
  text
end
strip = ->(text) { text.lines.reject { |line| line.start_with?('#include ', '__KERNEL_RCSID(') }.join }
extract = lambda do |text, name|
  first = text.index(/^#{name}\(/)
  abort 'Missing function ' + name unless first
  first = text.rindex(/^static\b/, first)
  last = text.index(/^\}/, first)
  abort 'Missing function end ' + name unless last
  text[first..last] + "\n"
end
begin
  driver = 'sys/external/bsd/drm2/bcmv3d/'
  { 'bo-body.h' => driver + 'bcmv3d_bo.c', 'drv-body.h' => driver + 'bcmv3d_drv.h',
    'uapi-body.h' => 'sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h'
  }.each { |name, path| bodies[name] = strip.call(read.call(path)) }
  job = read.call(driver + 'bcmv3d_job.c')
  driver_text = read.call(driver + 'bcmv3d_drv.c')
  read.call(driver + 'bcmv3d_bo_stats.h')
  %w[ember/tools/bcm2712-v3d-bo-contract.c ember/tools/bcmv3d-bo-size-contract.c
     ember/tools/bcmv3d-bo-size-contract.rb].each { |path| read.call(path) }
  definitions = %w[V3D_COPY_SIZE V3D_PT_SIZE V3D_PTE_VALID V3D_PTE_WRITE].map do |name|
    job[/^#define #{name}\s+[^\n]+/] || abort('Missing engine constant ' + name)
  end.join("\n")
  bodies['size-body.h'] = definitions + "\n" + extract.call(job, 'bcmv3d_ptes') +
    extract.call(driver_text, 'bcmv3d_prime_mmap')
  bodies.each { |name, text| File.write(dir + '/' + name, text) }
  run = lambda do |name, argv, required = true|
    out = ''; err = ''; status = nil; timeout = false
    Open3.popen3(*argv, pgroup: true) do |stdin, stdout, stderr, waiter|
      stdin.close
      readers = [Thread.new { out = stdout.read }, Thread.new { err = stderr.read }]
      unless waiter.join(name.end_with?('compile') ? 180 : 90)
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
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) +
    %w[-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + dir,
    source + '/ember/tools/bcmv3d-bo-size-contract.c', '-o', dir + '/contract']
  page64 = compile.dup.insert(1, '-DPAGE_SIZE=65536U'); page64[-1] = dir + '/contract-page64'
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
  [[compile, 'page4'], [page64, 'page64']].each do |argv, name|
    run.call(name + '-compile', argv)
    out, = run.call(name, execute + [argv[-1]]); print out
  end
  unless ENV['BO_SIZE_SKIP_MUTANTS'] == '1'
    segment = "\tbo->dmasegs = kmem_zalloc(bo->maxsegs * sizeof(*bo->dmasegs),\n\t    KM_SLEEP);\n"
    controls = {
      'old-16MiB-request' => ['bo-body.h', 'requested > BCMV3D_BO_MAX', 'requested > 16 * 1024 * 1024', compile],
      'maximum-inclusive' => ['bo-body.h', 'requested > BCMV3D_BO_MAX', 'requested >= BCMV3D_BO_MAX', compile],
      'quota-before-segments' => ['bo-body.h', segment, '', compile],
      'va-before-segments' => ['bo-body.h', segment, '', compile],
      'reservation-unwind' => ['bo-body.h', 'sc->allocated -= size;', '(void)sc;', compile],
      'native-page-rounding' => ['bo-body.h', '((size_t)requested + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)', '((size_t)requested + 4095) & ~((size_t)4095)', page64],
      'map-whole-BO' => ['size-body.h', 'job->nbo ? bo->cma.dmasize : V3D_COPY_SIZE', 'job->nbo ? MIN(bo->cma.dmasize, 16 * 1024 * 1024) : V3D_COPY_SIZE', compile],
      'whole-mmap-bound' => ['size-body.h', 'size > obj->size - (size_t)*offp ||', 'false ||', compile]
    }
    controls.each do |name, (file, before, after, argv)|
      text = bodies[file]
      abort 'Nonunique control ' + name unless text.scan(before).size == 1
      text = text.sub(before) { after }
      if name == 'quota-before-segments'
        text = text.sub("\tmutex_enter(&sc->memory_lock);\n\tif (size >", segment + "\tmutex_enter(&sc->memory_lock);\n\tif (size >")
      elsif name == 'va-before-segments'
        text = text.sub("\terror = vmem_alloc(sc->va, size,", segment + "\terror = vmem_alloc(sc->va, size,")
      end
      File.write(dir + '/' + file, text)
      run.call(name + '-compile', argv)
      _, err, status, timeout = run.call(name, execute + [argv[-1]], false)
      abort 'Noncausal control ' + name unless !timeout && status.exited? && status.exitstatus == 1 && err.include?('FAIL line ')
      puts 'PASS compiled FAIL control ' + name
      File.write(dir + '/' + file, bodies[file])
    end
  end
  inputs.each do |path, sha|
    abort 'Input changed during checks: ' + path unless Digest::SHA256.file(source + '/' + path).hexdigest == sha
  end
ensure
  File.write(dir + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(dir + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(dir) unless evidence
end
