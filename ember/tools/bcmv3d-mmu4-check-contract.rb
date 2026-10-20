#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'digest'
require 'json'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['MMU4_CHECK_EVIDENCE']
abort 'evidence must be new' if evidence && File.exist?(evidence)
evidence ||= Dir.mktmpdir('bcmv3d-mmu4-check-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(evidence)
puts 'Unused MMU4 check directory: ' + evidence
Process.setrlimit(Process::RLIMIT_CORE, 0, 0)
inputs = {}; commands = []
strip = ->(s) { s.lines.reject { |l| l.start_with?('#include ') }.join }
read = lambda do |p|
  s = File.binread(source + '/' + p)
  inputs[p] = Digest::SHA256.hexdigest(s)
  s
end
run = lambda do |name, argv, required = true|
  out = err = ''; status = nil; timeout = false
  Open3.popen3(*argv, pgroup: true) do |i, o, e, w|
    i.close
    readers = [Thread.new { out = o.read }, Thread.new { err = e.read }]
    unless w.join(30)
      timeout = true
      begin Process.kill('KILL', -w.pid); rescue Errno::ESRCH; end
    end
    status = w.value
    readers.each(&:join)
  end
  commands << {name: name, argv: argv, status: status.exitstatus, signal: status.termsig, timeout: timeout}
  File.write(evidence + '/' + name + '.stdout', out)
  File.write(evidence + '/' + name + '.stderr', err)
  abort name + ': ' + err if required && (timeout || !status.success?)
  [out, err, status, timeout]
end
begin
  paths = {
    'check-types.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_mmu4_check.h',
    'stats-types.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h',
    'check-body.h' => 'sys/external/bsd/drm2/bcmv3d/bcmv3d_mmu4_check.c'
  }
  paths.each { |name, p| File.write(evidence + '/' + name, strip.call(read.call(p))) }
  fixture = source + '/ember/tools/bcmv3d-mmu4-check-contract.c'
  read.call('ember/tools/bcmv3d-mmu4-check-contract.c')
  read.call('ember/tools/bcmv3d-mmu4-check-contract.rb')
  argv = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] + Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + evidence, fixture, '-o', evidence + '/contract']
  run.call('positive-compile', argv)
  FileUtils.cp(evidence + '/contract', ENV['MMU4_CHECK_OUTPUT']) if ENV['MMU4_CHECK_OUTPUT']
  unless ENV['MMU4_CHECK_COMPILE_ONLY'] == '1'
    executor = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
    out, = run.call('positive', executor + [evidence + '/contract']); print out
    unless ENV['MMU4_CHECK_SKIP_MUTANTS'] == '1'
      original = File.read(evidence + '/check-body.h')
      mutations = {
        'one-cycle' => ['cycle < 2', 'cycle < 1'],
        'caller-reference' => ['drm_gem_object_put_unlocked(&bo->cma.base);', '(void)bo;'],
        'discard-unpublished' => ['if ((owned & (1U << slot)) != 0)', 'if ((owned & (1U << slot)) != 0 && slot >= 3)'],
        'unmap-owned' => ['owned &= ~(1U << slot);', '(void)owned;'],
        'error-precedence' => ['s->first_error = error;', 'error = s->first_error = 0;'],
        'byte-accounting' => ['s->after_bytes != s->before_bytes ||', 'false ||'],
        'reservation-accounting' => ['s->reservations != 6 ||', 'false ||'],
        'release-accounting' => ['s->releases != 6 ||', 'false ||'],
        'failure-accounting' => ['after.failures != before.failures', 'false']
      }
      mutations.each do |name, (before, after)|
        abort 'nonunique source mutant ' + name unless original.scan(before).length == 1
        File.write(evidence + '/check-body.h', original.sub(before) { after })
        run.call(name + '-compile', argv)
        _, err, status, timeout = run.call(name, executor + [evidence + '/contract'], false)
        abort 'noncausal source rejection ' + name unless !timeout && status.signaled? && status.termsig == 6 && err.include?('CHECK ')
        puts 'PASS compiled CHECK/SIGABRT control ' + name
      end
      File.write(evidence + '/check-body.h', original)
    end
  end
ensure
  File.write(evidence + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(evidence) unless ENV['MMU4_CHECK_EVIDENCE']
end
