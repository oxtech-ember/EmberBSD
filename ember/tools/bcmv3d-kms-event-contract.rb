#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'

# Execute unchanged production bodies. The fixture supplies kernel services.
def extract_function(body, name)
  signature = /^(?:static )?void(?:\s+)#{Regexp.escape(name)}\s*\(/
  match = signature.match(body) or abort 'Missing function: ' + name
  first = body.index('{', match.end(0)) or abort 'Missing body: ' + name
  depth = 0
  tail = body[first..]
  tail.to_enum(:scan, %r{/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]}m).each do
    token = Regexp.last_match
    depth += 1 if token[0] == '{'
    depth -= 1 if token[0] == '}'
    return body[match.begin(0)...first + token.end(0)] + "\n" if depth.zero?
  end
  abort 'Unclosed body: ' + name
end

source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['KMS_EVENT_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('bcmv3d-kms-event-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(dir)
inputs = {}
commands = []
begin
  read = lambda do |path|
    body = File.binread(source + '/' + path)
    inputs[path] = Digest::SHA256.hexdigest(body)
    body
  end
  core = ''
  {
    'sys/external/bsd/drm2/dist/drm/drm_file.c' => %w[drm_events_release drm_event_cancel_free],
    'sys/external/bsd/drm2/dist/drm/drm_atomic_helper.c' => %w[release_crtc_commit],
    'sys/external/bsd/drm2/dist/drm/drm_atomic_state_helper.c' => %w[__drm_atomic_helper_crtc_destroy_state],
    'sys/external/bsd/drm2/dist/drm/drm_atomic.c' => %w[drm_atomic_state_default_clear]
  }.each do |path, names|
    body = read.call(path)
    names.each { |name| core += "/* Unchanged function from #{path}. */\n" + extract_function(body, name) }
  end
  target_errno = read.call('sys/sys/errno.h')
  abort 'Target ELAST changed: update the strict fixture bound' unless target_errno.match?(/^#define\s+ELAST\s+98\s/)
  helper = read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_kms_event.c')
  types = read.call('sys/external/bsd/drm2/bcmv3d/bcmv3d_kms_event.h')
  File.write(dir + '/core-bodies.h', core)
  File.write(dir + '/event-body.h', helper.lines.reject { |line| line.start_with?('#include ') }.join)
  File.write(dir + '/event-types.h', types)
  %w[c rb].each { |ext| read.call('ember/tools/bcmv3d-kms-event-contract.' + ext) }
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
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=gnu11 -Wall -Wextra -Werror] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + dir,
    source + '/ember/tools/bcmv3d-kms-event-contract.c', '-o', dir + '/contract']
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [dir + '/contract']
  run.call('compile', compile)
  out, = run.call('positive', execute)
  print out
  unless ENV['KMS_EVENT_SKIP_CONTROLS'] == '1'
    controls = {
      'error-sign' => ['event-body.h', 'dma_fence_set_error(pending->fence, -error);', 'dma_fence_set_error(pending->fence, error);'],
      'error-bound' => ['event-body.h', 'error > ELAST', 'error > 4095'],
      'error-normalization' => ['event-body.h', 'error <= 0 || error > ELAST', 'false'],
      'completion-signal' => ['event-body.h', 'complete_all(completion);', 'if (false) complete_all(completion);'],
      'completion-release' => ['event-body.h', 'completion_release(completion);', 'if (false) completion_release(completion);'],
      'completion-detach' => ['event-body.h', 'pending->completion = NULL;', 'if (false) pending->completion = NULL;'],
      'release-detach' => ['event-body.h', 'pending->completion_release = NULL;', 'if (false) pending->completion_release = NULL;'],
      'fence-signal' => ['event-body.h', 'dma_fence_signal(pending->fence);', 'if (false) dma_fence_signal(pending->fence);'],
      'event-cancel' => ['event-body.h', 'drm_event_cancel_free(dev, pending);', 'if (false) drm_event_cancel_free(dev, pending);'],
      'no-success-event' => ['event-body.h', 'drm_event_cancel_free(dev, pending);', 'drm_send_event_locked(dev, pending);'],
      'cancel-fence-reference' => ['core-bodies.h', 'dma_fence_put(p->fence);', 'if (false) dma_fence_put(p->fence);'],
      'cancel-event-space' => ['core-bodies.h', 'p->file_priv->event_space += p->event->length;', 'p->file_priv->event_space += 0;'],
      'close-detach' => ['core-bodies.h', 'e->file_priv = NULL;', 'if (false) e->file_priv = NULL;'],
      'abort-reference' => ['core-bodies.h', 'if (state->event && state->commit->abort_completion)', 'if (false && state->event && state->commit->abort_completion)'],
      'atomic-entry-reference' => ['core-bodies.h', 'drm_crtc_commit_put(state->crtcs[i].commit);', 'if (false) drm_crtc_commit_put(state->crtcs[i].commit);']
    }
    originals = %w[event-body.h core-bodies.h].to_h { |path| [path, File.read(dir + '/' + path)] }
    controls.each do |name, (path, before, after)|
      original = originals.fetch(path)
      abort 'Unique source control required: ' + name unless original.scan(before).size == 1
      File.write(dir + '/' + path, original.sub(before) { after })
      run.call(name + '-compile', compile)
      _, err, status, timeout = run.call(name, execute, false)
      abort 'Noncausal source control: ' + name unless !timeout && status.exited? &&
        status.exitstatus == 1 && err.lines.any? { |line| line.start_with?('CHECK ') }
      puts 'PASS compiled CHECK control ' + name
      File.write(dir + '/' + path, original)
    end
  end
ensure
  File.write(dir + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(dir + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(dir) unless evidence
end
