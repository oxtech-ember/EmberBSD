#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json'
require 'digest'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['RESV_EVIDENCE']
abort 'new evidence required' if evidence && File.exist?(evidence)
evidence ||= Dir.mktmpdir('bcmv3d-resv-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(evidence)
puts 'BO reservation contract directory: ' + evidence
Process.setrlimit(Process::RLIMIT_CORE, 0, 0)
inputs = {}; commands = []
read = lambda do |p|
  bytes = File.binread(source + '/' + p)
  inputs[p] = Digest::SHA256.hexdigest(bytes)
  bytes
end
strip = ->(s) { s.lines.reject { |l| l.start_with?('#include ') }.join }
run = lambda do |name, argv, required = true, limit = 30|
  out = err = ''; status = nil; timeout = false
  Open3.popen3(*argv, pgroup: true) do |i, o, e, w|
    i.close
    readers = [Thread.new { out = o.read }, Thread.new { err = e.read }]
    unless w.join(limit)
      timeout = true
      begin Process.kill('KILL', -w.pid); rescue Errno::ESRCH; end
    end
    status = w.value
    readers.each(&:join)
  end
  commands << {name: name, argv: argv, exit: status.exitstatus, signal: status.termsig, timeout: timeout}
  File.write(evidence + '/' + name + '.stdout', out)
  File.write(evidence + '/' + name + '.stderr', err)
  abort name + ': ' + err if required && (timeout || !status.success?)
  [out, err, status, timeout]
end
begin
  base = 'sys/external/bsd/drm2/bcmv3d/'
  File.write(evidence + '/resv-types.h', strip.call(read.call(base + 'bcmv3d_resv.h')))
  body = read.call(base + 'bcmv3d_resv.c')
  File.write(evidence + '/resv-body.h', strip.call(body))
  gem = read.call('sys/external/bsd/drm2/dist/drm/drm_gem.c')
  functions = %w[drm_gem_lock_reservations drm_gem_unlock_reservations].map do |n|
    matches = gem.scan(/^(?:int|void)\n#{n}\(.*?^}/m)
    abort 'GEM function boundary ' + n unless matches.size == 1
    matches.first
  end
  File.write(evidence + '/gem-lock-body.h', gem.split(/^#include /, 2).first + functions.join("\n"))
  read.call('ember/tools/bcmv3d-resv-contract.c')
  read.call('ember/tools/bcmv3d-resv-contract.rb')
  argv = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] + Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + evidence, source + '/ember/tools/bcmv3d-resv-contract.c', '-o', evidence + '/contract']
  run.call('positive-compile', argv)
  FileUtils.cp(evidence + '/contract', ENV['RESV_OUTPUT']) if ENV['RESV_OUTPUT']
  unless ENV['RESV_COMPILE_ONLY'] == '1'
    executor = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
    out, = run.call('positive', executor + [evidence + '/contract']); print out
    causal = ->(err, status, timeout) { !timeout && status.signaled? && status.termsig == 6 && err.match?(/^CHECK \d+: /) }
    unless ENV['RESV_SKIP_HARNESS'] == '1'
      %w[segv abort hang check].each do |control|
        _, err, status, timeout = run.call('harness-' + control, ['env', 'RESV_HARNESS_CONTROL=' + control] + executor + [evidence + '/contract'], false, control == 'hang' ? 1 : 30)
        abort 'invalid harness classification ' + control unless causal.call(err, status, timeout) == (control == 'check')
      end
    end
    unless ENV['RESV_SKIP_MUTANTS'] == '1'
      original = File.read(evidence + '/resv-body.h')
      mutations = {
        'ww-error-fini' => ['ww_acquire_fini(&s->acquire);', '(void)s;'],
        'exclusive-wait' => ['error = bcmv3d_resv_wait(exclusive, &remaining);', 'error = ((void)exclusive, 0);'],
        'shared-wait' => ['for (j = 0; error == 0 && j < nshared; j++)', 'for (j = 0; error == 0 && j < nshared && i >= count; j++)'],
        'fence-status' => ['return status > 0 ? 0 : status == 0 ? EIO : -status;', 'return ((void)status, 0);'],
        'shared-put' => ['dma_fence_put(shared[j]);', '(void)shared[j];'],
        'snapshot-free' => ['kfree(shared);', '(void)shared;'],
        'failure-unlock' => ["fail:\n\tbcmv3d_resv_unlock(s);", "fail:\n\t(void)s;"],
        'actual-owner' => ['dma_resv_locking_ctx(s->objects[i]->resv) != &s->acquire', 'false'],
        'complete-owner-preflight' => ["for (i = 0; i < s->count; i++) {", "for (i = 0; i + 1 < s->count; i++) {"],
        'pending-fence' => ['if (dma_fence_get_status(fence) != 0)', 'if (dma_fence_get_status(fence) < 0)'],
        'duplicate-reservation' => ['objects[i]->resv == objects[j]->resv', 'false'],
        'remaining-budget' => ['*remaining = waited;', '(void)waited;'],
        'timeout-result' => ['return waited == 0 ? ETIME : -waited;', 'return waited == 0 ? 0 : -waited;']
      }
      expected_checks = {
        "ww-error-fini" => "locks == unlocks && contexts == 0 && frees == arrays",
        "exclusive-wait" => "state.locked && waits == 192 && snapshots == 64 && publications == 0",
        "shared-wait" => "state.locked && waits == 192 && snapshots == 64 && publications == 0",
        "fence-status" => "error == (kind <= 2 || kind == 5 ? EINTR : kind == 3 ? ENOMEM : kind == 4 ? ETIME : kind == 8 ? ERESTARTSYS : EIO)",
        "shared-put" => "initial[i][j].refs == (publications != 0 ? 1U : 2U)",
        "snapshot-free" => "locks == unlocks && contexts == 0 && frees == arrays",
        "failure-unlock" => "!state.locked && publications == 0",
        "actual-owner" => "bcmv3d_resv_publish(&copied, &producer) == EPERM && publications == 0",
        "complete-owner-preflight" => "bcmv3d_resv_publish(&state, &producer) == EPERM && publications == 0",
        "pending-fence" => "engine_locked && r->owner != NULL && r->owner->done && f == &producer",
        "duplicate-reservation" => "!engine_locked && r->owner == NULL && ctx->initialized && !ctx->done",
        "remaining-budget" => "timeout == budget",
        "timeout-result" => "error == (kind <= 2 || kind == 5 ? EINTR : kind == 3 ? ENOMEM : kind == 4 ? ETIME : kind == 8 ? ERESTARTSYS : EIO)"
      }
      mutations.each do |name, (before, after)|
        abort 'nonunique source control ' + name unless original.scan(before).length == 1
        File.write(evidence + '/resv-body.h', original.sub(before) { after })
        run.call(name + '-compile', argv)
        _, err, status, timeout = run.call(name, executor + [evidence + '/contract'], false)
        abort 'noncausal compiled control ' + name unless causal.call(err, status, timeout)
        named = err.lines.any? { |line| line.match?(/^CHECK \d+: /) && line.sub(/^CHECK \d+: /, '').strip == expected_checks.fetch(name) }
        abort 'wrong CHECK for source control ' + name unless named
        puts 'PASS compiled CHECK/SIGABRT control ' + name
      end
      File.write(evidence + '/resv-body.h', original)
    end
  end
ensure
  File.write(evidence + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(evidence) unless ENV['RESV_EVIDENCE']
end
