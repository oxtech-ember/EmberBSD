#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
require 'timeout'
Process.setrlimit(:CORE, 0, 0)
def capture(command, seconds = 60)
  Open3.popen3(*command, pgroup: true) do |input, output, errors, waiter|
    input.close
    stdout = Thread.new { output.read }
    stderr = Thread.new { errors.read }
    begin
      return Timeout.timeout(seconds) { [stdout.value, stderr.value, waiter.value] }
    rescue Timeout::Error
      begin Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
      waiter.value
      raise 'command timed out: ' + command.shelljoin
    ensure
      stdout.join
      stderr.join
    end
  end
end
root = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['SNAPSHOT_EVIDENCE']
abort 'new evidence directory required' if evidence && File.exist?(evidence)
work = evidence || Dir.mktmpdir('kms-snapshot-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(work)
paths = %w[ember/tools/bcmv3d-kms-snapshot.c ember/tools/bcmv3d-kms-snapshot-contract.c ember/tools/bcmv3d-kms-snapshot-contract.rb]
uapi = root + '/sys/external/bsd/drm2/dist/include/uapi/drm'
%w[drm.h drm_mode.h drm_fourcc.h v3d_drm.h].each { |p| paths << 'sys/external/bsd/drm2/dist/include/uapi/drm/' + p }
pins = paths.map { |p| [p, Digest::SHA256.file(root + '/' + p).hexdigest] }.to_h
commands = []
run = lambda do |name, args, expected = 0, condition = nil|
  out, err, status = capture(args)
  commands << { name: name, argv: args, exit: status.exitstatus, signal: status.termsig }
  File.write(work + '/' + name + '.stdout', out)
  File.write(work + '/' + name + '.stderr', err)
  abort 'unexpected exit: ' + name unless status.exited? && status.exitstatus == expected
  if condition
    abort 'wrong causal condition: ' + name unless err.lines.any? { |l| l.sub(/^FAIL contract: /, '').strip == condition && l.start_with?('FAIL contract: ') }
  else
    abort 'missing controlled result: ' + name unless name.end_with?('-compile') || out.include?('PASS controlled snapshot scenario')
  end
end
begin
  original = File.read(root + '/ember/tools/bcmv3d-kms-snapshot.c')
  body = work + '/snapshot-body.h'
  File.write(body, original)
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] + Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + uapi, '-I' + work, root + '/ember/tools/bcmv3d-kms-snapshot-contract.c', '-o', work + '/contract']
  run.call('positive-compile', compile)
  scenarios = %w[pass page64 high-offset short eintr bad-page alloc open version driver plane-first blank no-crtc format two-formats get-fb no-handle width height pitch depth bpp wait wait-timeout map-offset unaligned offset-overflow mmap seq-before inactive-before plane-before changed-before plane-after changed-after changed-crtc seq-after inactive-after seq-backwards time-backwards munmap gem-close close-card exists create write-header write-rgb write-zero fsync close-output]
  scenarios.each { |s| run.call(s, [work + '/contract', s]) }
  unless ENV['SNAPSHOT_SKIP_CONTROLS'] == '1'
    result = 'result == (success ? 0 : 1)'
    mutations = {
      'before-frame' => ['if (!snapshot_same_plane(&first, &before))', 'if (false && !snapshot_same_plane(&first, &before))', 'changed-before', result],
      'after-frame' => ['if (!snapshot_same_plane(&first, &after))', 'if (false && !snapshot_same_plane(&first, &after))', 'changed-after', result],
      'width' => ['fb.width != SNAP_WIDTH', 'false', 'width', result],
      'sequence' => ['seq1.sequence < seq0.sequence', 'false', 'seq-backwards', result],
      'wait-error' => ['if (ioctl(fd, DRM_IOCTL_V3D_WAIT_BO, &wait) == -1)', 'if (ioctl(fd, DRM_IOCTL_V3D_WAIT_BO, &wait) == -1 && false)', 'wait', result],
      'RGB-order' => ['rgb[3 * i] = map[4 * i + 2];', 'rgb[3 * i] = map[4 * i];', 'pass', 'bytes[j] == expected'],
      'X-is-not-alpha' => ['rgb[3 * i] = map[4 * i + 2];', 'rgb[3 * i] = map[4 * i + 3];', 'pass', 'bytes[j] == expected'],
      'full-last-pixel' => ['i < (size_t)SNAP_WIDTH * SNAP_HEIGHT', 'i + 1 < (size_t)SNAP_WIDTH * SNAP_HEIGHT', 'pass', 'bytes[j] == expected'],
      'exclusive-file' => ['O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC', 'O_WRONLY | O_CREAT | O_CLOEXEC', 'pass', 'flags == (O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC)'],
      'one-second-wait' => ['wait.timeout_ns = UINT64_C(1000000000);', 'wait.timeout_ns = UINT64_C(2000000000);', 'pass', 'w->timeout_ns == UINT64_C(1000000000)'],
      'cleanup-handle' => ['ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gem) == -1', '((void)gem, false)', 'pass', 'gem_closes == 1 && unmaps == 1 && mapped == NULL'],
      'unlink-on-error' => ['created && unlink(path) == -1', '((void)created, false) && unlink(path) == -1', 'write-rgb', 'image == success'],
      'rounded-map' => ['length = (SNAP_BYTES + (size_t)page - 1) & ~((size_t)page - 1);', 'length = SNAP_BYTES;', 'page64', 'n == ((SNAP_BYTES + (size_t)pagesize - 1) & ~((size_t)pagesize - 1))'],
      'offset-width' => ['(off_t)offset', '(off_t)(uint32_t)offset', 'high-offset', '(uint64_t)off == map_offset']
    }
    mutations.each do |name, (before, after, scenario, condition)|
      abort 'nonunique control: ' + name unless original.scan(Regexp.new(Regexp.escape(before))).size == 1
      File.write(body, original.sub(before, after))
      run.call(name + '-compile', compile)
      run.call(name, [work + '/contract', scenario], 1, condition)
      File.write(body, original)
    end
  end
  run.call('final-compile', compile)
  pins.each { |p, h| abort 'source changed: ' + p unless Digest::SHA256.file(root + '/' + p).hexdigest == h }
  puts 'PASS actual-source snapshot scenarios and named compiled controls'
ensure
  File.write(work + '/receipt.json', JSON.pretty_generate({ inputs_sha256: pins, commands: commands, scope: 'Controlled host DRM/syscalls; no target, scanout, DMA or cache acceptance.' }) + "\n")
  puts 'Evidence: ' + work
  FileUtils.remove_entry(work) unless evidence
end
