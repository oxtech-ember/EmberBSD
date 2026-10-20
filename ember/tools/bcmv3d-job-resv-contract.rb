#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'json'
require 'digest'
require 'fileutils'
require 'tmpdir'
require 'shellwords'
require 'open3'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
output = ENV['V3D_RESV_EVIDENCE']
abort 'new evidence directory required' if output && File.exist?(output)
output ||= Dir.mktmpdir('v3d-job-resv-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(output)
Process.setrlimit(Process::RLIMIT_CORE, 0, 0)
inputs = {}; commands = []
read = ->(path) { text = File.binread(source + '/' + path); inputs[path] = Digest::SHA256.hexdigest(text); text }
strip = ->(text) { text.lines.reject { |line| line.start_with?('#include ', '__KERNEL_RCSID(') }.join }
run = lambda do |name, argv, required = true|
  out = err = ''; status = nil; timeout = false
  Open3.popen3(*argv, pgroup: true) do |i, o, e, w|
    i.close
    readers = [Thread.new { out = o.read }, Thread.new { err = e.read }]
    unless w.join(30)
      timeout = true
      begin Process.kill('KILL', -w.pid); rescue Errno::ESRCH; end
    end
    status = w.value; readers.each(&:join)
  end
  commands << {name: name, argv: argv, exit: status.exitstatus, signal: status.termsig, timeout: timeout}
  File.write(output + '/' + name + '.stdout', out); File.write(output + '/' + name + '.stderr', err)
  abort name + ': ' + err if required && (timeout || !status.success?)
  [out, err, status, timeout]
end
begin
  %w[bo job drv].each do |name|
    ext = name == 'drv' ? 'h' : 'c'
    File.write(output + '/' + name + '-body.h', strip.call(read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_' + name + '.' + ext)))
  end
  File.write(output + '/uapi-body.h', strip.call(read.call('sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h')))
  %w[h c].each { |ext| read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_resv.' + ext) }
  read.call('sys/external/bsd/drm2/dist/drm/drm_gem.c')
  read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h')
  Dir[source + '/ember/tools/bcm2712-v3d-*-contract.c', source + '/ember/tools/bcm2712-v3d-*-model.h'].sort.each { |p| read.call(p.delete_prefix(source + '/')) }
  %w[bcmv3d-job-resv-contract.c bcmv3d-job-resv-contract.rb bcmv3d-resv-model-export.rb].each { |n| read.call('ember/tools/' + n) }
  run.call('export', ['ruby', source + '/ember/tools/bcmv3d-resv-model-export.rb', source, output])
  argv = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function] + Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + output, source + '/ember/tools/bcmv3d-job-resv-contract.c', '-o', output + '/contract']
  run.call('positive-compile', argv)
  FileUtils.cp(output + '/contract', ENV['V3D_OUTPUT']) if ENV['V3D_OUTPUT']
  unless ENV['V3D_COMPILE_ONLY'] == '1'
    executor = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
    out, = run.call('positive', executor + [output + '/contract']); print out
    unless ENV['V3D_SKIP_MUTANTS'] == '1'
      original = File.read(output + '/job-body.h')
      changes = {'no-native-fence' => ["\tif (job->fence_seqno == UINT64_MAX)", "\tif (sync->output == NULL) return 0;\n\tif (job->fence_seqno == UINT64_MAX)", 1]}
      %w[tfu csd cl].each_with_index do |name, index|
        changes[name + '-prepare'] = [/error = bcmv3d_resv_prepare\(&reservations, objects, (?:2|count),\n\t    MAX\(1, mstohz\(1000\)\)\);/, 'error = 0;', index + 1]
        changes[name + '-publish'] = ['error = bcmv3d_resv_publish(&reservations, sync.fence);', 'error = 0;', index + 1]
        changes[name + '-unlock'] = ["\t/* Completion cannot need a reservation held by this submission. */\n\tbcmv3d_resv_unlock(&reservations);", "\t/* Fault control: hold reservations through execution. */", index + 1]
      end
      expected = {
        'no-native-fence' => 'error == expected',
        'tfu-prepare' => 'submitted[0]->resv->exclusive == f && submitted[1]->resv->exclusive == f', 'csd-prepare' => 'submitted[0]->resv->exclusive == f && submitted[1]->resv->exclusive == f', 'cl-prepare' => 'submitted[0]->resv->exclusive == f && submitted[1]->resv->exclusive == f',
        'tfu-publish' => 'next && next->status == 0 && next->context == job->fence_context &&     next->seqno == job->fence_seqno',
        'csd-publish' => 'next && next->status == 0 && next->context == job->fence_context &&     next->seqno == job->fence_seqno',
        'cl-publish' => 'next && next->status == 0 && next->context == job->fence_context &&     next->seqno == job->fence_seqno',
        'tfu-unlock' => 'resv_locks == 0 && resv_contexts == 0 && resv_arrays == 0',
        'csd-unlock' => 'resv_locks == 0 && resv_contexts == 0 && resv_arrays == 0',
        'cl-unlock' => 'resv_locks == 0 && resv_contexts == 0 && resv_arrays == 0'
      }
      changes.each do |name, (before, after, nth)|
        seen = 0
        mutated = original.gsub(before) { |match| seen += 1; seen == nth ? after : match }
        abort 'source control anchor ' + name unless seen == (name == 'no-native-fence' ? 1 : 3)
        File.write(output + '/job-body.h', mutated)
        run.call(name + '-compile', argv)
        _, err, status, timeout = run.call(name, executor + [output + '/contract'], false)
        failure = err.lines.grep(/^FAIL line \d+: /).map { |line| line.sub(/^FAIL line \d+: /, '').gsub(/\s+/, ' ').strip }
        abort 'noncausal control ' + name + ': ' + err unless !timeout && status.exited? && status.exitstatus == 1 && failure == [expected.fetch(name).gsub(/\s+/, ' ')]
        puts 'PASS named compiled control ' + name
      end
      File.write(output + '/job-body.h', original)
    end
  end
ensure
  File.write(output + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(output + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(output) unless ENV['V3D_RESV_EVIDENCE']
end
