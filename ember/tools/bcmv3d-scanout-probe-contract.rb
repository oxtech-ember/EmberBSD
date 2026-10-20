#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['SCANOUT_PROBE_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('scanout-probe-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(dir)
inputs = {}
commands = []
begin
  {
    'flip-types.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_flip.h',
    'flip-body.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_flip.c',
    'scanout-types.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_scanout.h',
    'vblank-types.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_vblank_native.h',
    'bo-stats.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h',
    'module-body.h' => 'sys/modules/bcmv3d_scanout_probe/bcmv3d_scanout_probe.c'
  }.each do |output, path|
    body = File.binread(source + '/' + path)
    inputs[path] = Digest::SHA256.hexdigest(body)
    File.write(dir + '/' + output, body.lines.reject { |line| line.start_with?('#include ') }.join)
  end
  %w[c rb].each do |ext|
    path = 'ember/tools/bcmv3d-scanout-probe-contract.' + ext
    inputs[path] = Digest::SHA256.file(source + '/' + path).hexdigest
  end
  run = lambda do |name, argv, required = true|
    out = ''; err = ''; status = nil; timeout = false
    Open3.popen3(*argv, pgroup: true) do |stdin, stdout, stderr, waiter|
      stdin.close
      readers = [Thread.new { out = stdout.read }, Thread.new { err = stderr.read }]
      unless waiter.join(name.end_with?('compile') ? 180 : 30)
        timeout = true
        begin; Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
      end
      status = waiter.value
      readers.each(&:join)
    end
    File.write(dir + '/' + name + '.stdout', out)
    File.write(dir + '/' + name + '.stderr', err)
    commands << { name: name, argv: argv, exit: status.exitstatus, signal: status.termsig, timeout: timeout }
    abort "#{name}: #{err}" if required && (timeout || !status.success?)
    [out, err, status, timeout]
  end
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + dir,
    source + '/ember/tools/bcmv3d-scanout-probe-contract.c', '-o', dir + '/contract']
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [dir + '/contract']
  run.call('compile', compile)
  out, = run.call('positive', execute)
  print out
  page64 = compile.dup.insert(1, '-DPAGE_SIZE=65536U')
  page64[-1] = dir + '/contract-page64'
  run.call('page64-compile', page64)
  out, = run.call('page64', Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [page64[-1]])
  print out
  unless ENV['SCANOUT_PROBE_SKIP_MUTANTS'] == '1'
    controls = {
      'exact-build' => ['memcmp(expected, buildinfo, length) != 0', 'false'],
      'no-KMS' => ['(minor->dev->driver->driver_features & DRIVER_MODESET) != 0', 'false'],
      'native-seal' => ['!mmu.sealed ||', 'false ||'],
      'IRQ-route' => ['!irq.routed ||', 'false ||'],
      'pristine-owner' => ['s->prepared != 0 ||', 'false ||'],
      'real-latch' => ['s->latches == cycle + 1', 'true'],
      'extra-frame' => ['s->retired == cycle &&', 'true &&'],
      'bounded-wait' => ['return ETIMEDOUT;', 'return 0;'],
      'retain-after-prepare' => ['ctx->attempted = true;', 'ctx->attempted = false;'],
      'final-snapshot' => ['if (ctx->accepted && (status_error != 0 ||', 'if (false && (status_error != 0 ||'],
      'BO-accounting' => ['ctx->after.releases ==\n\t    ctx->before.releases + 2', 'true']
    }
    original = File.read(dir + '/module-body.h')
    controls.each do |name, (before, after)|
      before = before.gsub('\\n', "\n").gsub('\\t', "\t")
      abort 'Unique source control required: ' + name unless original.scan(before).size == 1
      File.write(dir + '/module-body.h', original.sub(before) { after })
      run.call(name + '-compile', compile)
      _, err, status, timeout = run.call(name, execute, false)
      abort 'Noncausal source control: ' + name unless !timeout && status.exited? &&
        status.exitstatus == 1 && err.lines.any? { |line| line.start_with?('CHECK ') }
      puts 'PASS compiled CHECK control ' + name
    end
    File.write(dir + '/module-body.h', original)
  end
ensure
  File.write(dir + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(dir + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(dir) unless evidence
end
