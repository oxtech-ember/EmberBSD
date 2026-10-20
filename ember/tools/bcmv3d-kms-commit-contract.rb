#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV.fetch(0, File.join(__dir__, '../..')))
fixture = File.join(__dir__, 'bcmv3d-kms-commit-contract.c')
evidence = ENV['KMS_COMMIT_EVIDENCE']
abort 'new evidence directory required' if evidence && File.exist?(evidence)
work = evidence || Dir.mktmpdir('bcmv3d-kms-commit-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(work)
inputs = {}; paths = {}; commands = []; reports = []
read = lambda do |path, key|
  data = File.binread(path); paths[key] = path
  inputs[key] = Digest::SHA256.hexdigest(data); data
end
production = read.call(source + '/sys/external/bsd/drm2/bcmv3d/bcmv3d_kms.c', 'sys/external/bsd/drm2/bcmv3d/bcmv3d_kms.c')
api = read.call(source + '/sys/external/bsd/drm2/bcmv3d/bcmv3d_kms.h', 'sys/external/bsd/drm2/bcmv3d/bcmv3d_kms.h')
read.call(fixture, 'ember/tools/bcmv3d-kms-commit-contract.c')
read.call(__FILE__, 'ember/tools/bcmv3d-kms-commit-contract.rb')
errno = read.call(source + '/sys/sys/errno.h', 'sys/sys/errno.h')
linux_errno = read.call(source + '/sys/external/bsd/common/include/linux/errno.h', 'sys/external/bsd/common/include/linux/errno.h')
elast = errno.scan(/^#define\s+ELAST\s+(\d+)\b/)
abort 'unique target ELAST required' unless elast.size == 1
restart = linux_errno.scan(/^#define\s+ERESTARTSYS\s+\(ELAST\s*\+\s*1\)/)
abort 'unique target ERESTARTSYS required' unless restart.size == 1
# The payload types and event IDs are actual UAPI, not independent fixture layouts.
uapi = read.call(source + '/sys/external/bsd/drm2/dist/include/uapi/drm/drm.h', 'sys/external/bsd/drm2/dist/include/uapi/drm/drm.h')
uapi_types = %w[drm_event drm_event_vblank drm_event_crtc_sequence].map do |name|
  found = uapi.scan(/^struct #{name} \{.*?^};/m)
  abort 'unique event UAPI type required: ' + name unless found.size == 1
  found.first
end
uapi_ids = %w[DRM_EVENT_VBLANK DRM_EVENT_FLIP_COMPLETE DRM_EVENT_CRTC_SEQUENCE].map do |name|
  found = uapi.scan(/^#define\s+#{name}\s+0x[0-9a-fA-F]+\b/)
  abort 'unique event UAPI ID required: ' + name unless found.size == 1
  found.first
end
File.write(work + '/kms-event-uapi.h', (uapi_ids + uapi_types).join("\n") + "\n")
%w[sys/external/bsd/drm2/dist/drm/drm_vblank.c sys/external/bsd/drm2/dist/drm/drm_file.c
   sys/external/bsd/drm2/dist/drm/drm_atomic_helper.c sys/external/bsd/drm2/include/linux/ktime.h].each do |path|
  read.call(source + '/' + path, path)
end
File.write(work + '/target-errno.h', "#undef ELAST\n#define ELAST #{elast.first.first}\n#undef ERESTARTSYS\n#define ERESTARTSYS (ELAST+1)\n")

extract = lambda do |text, name|
  matches = text.scan(/^(?:static )?[^\n;{}]+\n#{Regexp.escape(name)}\(.*?^}/m)
  abort 'unique complete function required: ' + name unless matches.size == 1
  matches.first
end
names = %w[kms_plane_state kms_fault kms_available kms_vblank_counter kms_vblank_enable kms_vblank_disable kms_native_event kms_complete_event kms_commit_tail bcmv3d_kms_bind bcmv3d_kms_fini]
bodies = names.to_h { |name| [name, extract.call(production, name)] }
original = production.split(/^#include /, 2).first + bodies.values.join("\n\n") + "\n"
types = %w[bcmv3d_kms_plane_state bcmv3d_kms].map do |name|
  matches = production.scan(/^struct #{name} \{.*?^};/m)
  abort 'unique complete structure required: ' + name unless matches.size == 1
  matches.first
end.join("\n")
File.binwrite(work + '/kms-interface.h', api.lines.reject { |line| line.start_with?('#include ') }.join)
File.binwrite(work + '/kms-types.h', types)
File.binwrite(work + '/kms-body.h', original)
argv = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] + Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + work, fixture, '-o', work + '/contract']
run = lambda do |name, args, required = true|
  output = ''; error = ''; status = nil; timeout = false
  Open3.popen3(*args, pgroup: true) do |input, stdout, stderr, wait|
    input.close
    readers = [Thread.new { output = stdout.read }, Thread.new { error = stderr.read }]
    unless wait.join(name.end_with?('compile') ? 180 : 30)
      timeout = true
      begin Process.kill('KILL', -wait.pid); rescue Errno::ESRCH; end
    end
    status = wait.value; readers.each(&:join)
  end
  File.binwrite(work + '/' + name + '.stdout', output)
  File.binwrite(work + '/' + name + '.stderr', error)
  commands << {name: name, argv: args, exit: status.exitstatus, signal: status.termsig, timeout: timeout}
  abort name + ': ' + error if required && (timeout || !status.success?)
  [output, error, status, timeout]
end
replace = lambda do |body, before, after, count = 1|
  abort 'unique source control anchor required: ' + before unless body.scan(before).size == count
  body.gsub(before) { after }
end
begin
  run.call('compile', argv)
  FileUtils.cp(work + '/contract', ENV['KMS_COMMIT_OUTPUT']) if ENV['KMS_COMMIT_OUTPUT']
  unless ENV['KMS_COMMIT_COMPILE_ONLY'] == '1'
    execute = Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [work + '/contract']
    output, = run.call('positive', execute); reports << output
    %w[bound waiting event polling].each do |state|
      _, error, status, timeout = run.call('fini-invalid-' + state, execute + [state], false)
      checks = error.lines.grep(/^CHECK \d+: /).map { |line| line.sub(/^CHECK \d+: /, '').strip }
      expected = '!k->bound && !k->waiting && k->event == NULL && !k->polling'
      abort 'noncausal fini assertion ' + state + ': ' + error unless !timeout && status.exited? && status.exitstatus == 1 && checks == [expected]
      reports << 'PASS actual fini assertion ' + state + "\n"
    end
    unless ENV['KMS_COMMIT_SKIP_MUTANTS'] == '1'
      controls = {
        'fini-destroy-before-free' => ['bcmv3d_kms_fini', 'destroy_completion(&k->latch);', '(void)destroy_completion;', 'config_cleanups==1&&!completion_live&&completion_destroys==1&&frees==0'],
        'fault-errno-bound' => ['kms_fault', 'error <= ELAST', 'error <= ELAST+1', 'd==&dev&&!dev.event_lock.held&&error>0&&error<=ELAST'],
        'fault-zero-normalization' => ['kms_fault', 'error > 0 && error <= ELAST', 'error >= 0 && error <= ELAST', 'd==&dev&&!dev.event_lock.held&&error>0&&error<=ELAST'],
        'fault-negative-normalization' => ['kms_fault', 'error > 0 && error <= ELAST', 'error <= ELAST', 'd==&dev&&!dev.event_lock.held&&error>0&&error<=ELAST'],
        'frame-counter-not-IRQ' => ['kms_vblank_counter', '(uint32_t)status.frame_epoch', '(uint32_t)status.sequence', 'kms_vblank_counter(&kms.crtc)==403'],
        'bind-fault-before-publish' => ['bcmv3d_kms_bind', 'if (k->fault != 0)', 'if (false)', 'd==&dev&&kms.bound&&kms.fault==0&&!dev.event_lock.held'],
        'event-floor' => ['kms_native_event', 'sequence > k->event_floor', 'true', '!c->done&&kms.waiting&&events[0].terminal==0'],
        'older-latched-event' => ['kms_native_event', 'sequence > k->event_floor', 'count && sequence > k->event_floor', 'c->done!=0'],
        'publish-before-instant-IRQ' => ['kms_commit_tail', 'k->waiting = true;', 'k->waiting = false;', 'kms.waiting&&kms.event==(no_event?NULL:&events[hardware_done])&&crtc.event==NULL'],
        'explicit-error-status' => ['kms_commit_tail', 'if (error == 0 && s->explicit != NULL)', 'if (false)', 'plane.explicit==NULL||explicit_checked'],
        'finalize-before-submit' => ['kms_commit_tail', 'bcmv3d_scanout_finalize(s->frame, 5 * hz);', '((void)bcmv3d_scanout_finalize, 0);', 'f->ready'],
        'finalize-error-propagation' => ['kms_commit_tail', 'bcmv3d_scanout_finalize(s->frame, 5 * hz);', '((void)bcmv3d_scanout_finalize(s->frame, 5 * hz), 0);', 'f->ready'],
        'timeout-is-terminal-error' => ['kms_commit_tail', 'error = ETIMEDOUT;', 'error = 0;', 'kms.fault==error&&sends==0&&failures==1'],
        'one-vblank-on' => ['kms_commit_tail', 'if (!k->vblank_on)', 'if (true)', 'c==&kms.crtc&&!dev.event_lock.held&&!core_on'],
        'stale-count-suppression' => ['kms_native_event', 'sequence > k->vblank_sequence', 'true', 'handle_calls==1&&drm_frames==3'],
        'fault-clears-pending' => ['kms_fault', 'k->waiting = false;', 'k->waiting = true;', '!kms.waiting&&kms.event==NULL&&kms.latch.done==UINT32_MAX']
      }
      controls.merge!({
        'old-retirement-required' => ['kms_commit_tail', 'bcmv3d_scanout_wait_retired(old_frame, 5 * hz);',
          '((void)bcmv3d_scanout_wait_retired, 0);',
          'kms.fault!=0||old_plane.frame==NULL||old_plane.frame==plane.frame||old_plane.frame->retired'],
        'same-frame-not-retired' => ['kms_commit_tail', 'old_frame != s->frame', 'true',
          'f==&old_frame&&ticks==5*hz&&!dev.event_lock.held&&!in_irq'],
        'captured-timestamp' => ['kms_commit_tail', 'latch_time = k->latch_time;', 'latch_time = ktime_get();',
          'e->event.vbl.tv_usec==(uint32_t)(tv.tv_nsec/1000)'],
        'captured-sequence' => ['kms_commit_tail', 'latch_sequence = k->latch_sequence;', 'latch_sequence = drm_frames;',
          'e->event.vbl.sequence==(uint32_t)captured_sequence'],
        'cleanup-fault-cancels-event' => ['kms_complete_event', 'error = k->fault;', 'error = 0;',
          'kms.fault==0&&e->terminal==0&&e->out_status==0']
      })
      mutants = controls.to_h do |name, (function, before, after, expected)|
        changed = replace.call(bodies.fetch(function), before, after)
        [name, [original.sub(bodies.fetch(function)) { changed }, expected]]
      end
      callback = replace.call(bodies.fetch('kms_native_event'),
        "k->latch_sequence = drm_crtc_vblank_count_and_time(&k->crtc,\n\t\t    &k->latch_time);",
        "if (k->event != NULL)\n\t\t\tdrm_crtc_send_vblank_event(&k->crtc, k->event);\n\t\tk->event = NULL;")
      mutants['original-IRQ-delivers-too-early'] = [original.sub(bodies.fetch('kms_native_event')) { callback },
        '!in_irq&&hardware_done==cleanup&&cleanup>0&&old_refs==0']
      tail = replace.call(bodies.fetch('kms_commit_tail'),
        "drm_atomic_helper_cleanup_planes(dev, state);\n\t/* cleanup_fb can discover a late fault. Recheck before signalling success. */\n\tkms_complete_event(k, event, latch_sequence, latch_time);",
        "kms_complete_event(k, event, latch_sequence, latch_time);\n\tdrm_atomic_helper_cleanup_planes(dev, state);")
      mutants['event-after-old-cleanup'] = [original.sub(bodies.fetch('kms_commit_tail')) { tail },
        '!in_irq&&hardware_done==cleanup&&cleanup>0&&old_refs==0']
      binding = replace.call(bodies.fetch('bcmv3d_kms_bind'), 'drm_crtc_vblank_off(&k->crtc);', '')
      binding = replace.call(binding, "sc->drm->irq_enabled = true;\n\tspin_unlock_irqrestore(&sc->drm->event_lock, flags);", "sc->drm->irq_enabled = true;\n\tspin_unlock_irqrestore(&sc->drm->event_lock, flags);\n\tdrm_crtc_vblank_off(&k->crtc);")
      mutants['bind-initial-off-before-admission'] = [original.sub(bodies.fetch('bcmv3d_kms_bind')) { binding }, 'kms_available(&kms)!=0']
      tail = replace.call(bodies.fetch('kms_commit_tail'), 's->explicit', 'plane->fence', 2)
      mutants['retain-cleared-explicit-fence'] = [original.sub(bodies.fetch('kms_commit_tail')) { tail }, 'plane.explicit==NULL||explicit_checked']
      tail = replace.call(bodies.fetch('kms_commit_tail'), "drm_atomic_helper_commit_hw_done(state);\n\tdrm_atomic_helper_cleanup_planes(dev, state);", "drm_atomic_helper_cleanup_planes(dev, state);\n\tdrm_atomic_helper_commit_hw_done(state);")
      mutants['old-cleanup-after-hw-done'] = [original.sub(bodies.fetch('kms_commit_tail')) { tail }, 'hardware_done==cleanup+1&&old_refs==1']
      tail = replace.call(bodies.fetch('kms_commit_tail'), 'drm_atomic_helper_commit_hw_done(state);', '(void)drm_atomic_helper_commit_hw_done;')
      tail = replace.call(tail, 'error = kms_available(k);', 'drm_atomic_helper_commit_hw_done(state); error = kms_available(k);')
      mutants['hw-done-after-terminal'] = [original.sub(bodies.fetch('kms_commit_tail')) { tail }, 's==&atomic&&!dev.event_lock.held&&crtc.event==NULL']
      mutants.each do |name, (body, expected)|
        File.binwrite(work + '/kms-body.h', body)
        run.call(name + '-compile', argv)
        _, error, status, timeout = run.call(name, execute, false)
        checks = error.lines.grep(/^CHECK \d+: /).map { |line| line.sub(/^CHECK \d+: /, '').strip }
        abort 'noncausal source control ' + name + ': ' + error unless !timeout && status.exited? && status.exitstatus == 1 && checks == [expected]
        reports << 'PASS compiled named KMS control ' + name + "\n"
      end
    end
  end
  verified = paths.to_h { |key, path| [key, Digest::SHA256.file(path).hexdigest] }
  File.write(work + '/verified-inputs.json', JSON.pretty_generate(verified) + "\n")
  abort 'source inputs changed during the contract run' unless verified == inputs
  reports.each { |report| print report }
ensure

  File.binwrite(work + '/kms-body.h', original)
  File.write(work + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(work + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(work) unless evidence
end
