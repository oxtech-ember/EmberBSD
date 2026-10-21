#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Actual native GEM checker with controlled host services. No target execution.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
Process.setrlimit(:CORE, 0, 0)

# Same reader/waiter deadline as the existing snapshot contract.
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

def accepted_result?(out, err, status, timeout, expected, condition = nil, compile = false)
  return false if timeout || !status || !status.exited? || status.exitstatus != expected
  return false if (out + err).match?(/AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|MemorySanitizer|ThreadSanitizer|runtime error:/)
  return true if compile
  if condition
    err.lines.any? { |line| line.start_with?('FAIL contract: ') &&
      line.sub(/^FAIL contract: /, '').strip == condition }
  else
    out.include?('PASS controlled mmap scenario')
  end
end

root = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
abort 'unexpected arguments' unless ARGV.length <= 1
evidence = ENV['MMAP_EVIDENCE']
abort 'new evidence directory required' if evidence && File.exist?(evidence)
work = evidence || Dir.mktmpdir('v3d-mmap-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(work)
paths = %w[ember/tools/bcm2712-v3d-mmap-check.c ember/tools/bcm2712-v3d-mmap-check-contract.c ember/tools/bcm2712-v3d-mmap-check-contract.rb]
uapi = root + '/sys/external/bsd/drm2/dist/include/uapi/drm'
%w[drm.h drm_mode.h v3d_drm.h].each { |p| paths << 'sys/external/bsd/drm2/dist/include/uapi/drm/' + p }
pins = paths.map { |p| [p, Digest::SHA256.file(root + '/' + p).hexdigest] }.to_h
commands = []
used_names = {}
execute = lambda do |name, args, seconds = 60|
  abort 'duplicate command tag: ' + name if used_names.key?(name) ||
    File.exist?(work + '/' + name + '.stdout') || File.exist?(work + '/' + name + '.stderr')
  used_names[name] = true
  out = ''; err = ''; status = nil; timed = false
  begin
    out, err, status = capture(args, seconds)
  rescue RuntimeError => exception
    raise unless exception.message.start_with?('command timed out:')
    timed = true; err = exception.message + "\n"
  end
  File.write(work + '/' + name + '.stdout', out)
  File.write(work + '/' + name + '.stderr', err)
  commands << {name:name, argv:args, exit:status && status.exitstatus,
    signal:status && status.termsig, timeout:timed,
    stdout_sha256:Digest::SHA256.hexdigest(out), stderr_sha256:Digest::SHA256.hexdigest(err)}
  [out, err, status, timed]
end
run = lambda do |name, args, expected = 0, condition = nil|
  out, err, status, timed = execute.call(name, args)
  abort 'rejected result: ' + name + "\n" + out + err unless accepted_result?(out, err, status,
    timed, expected, condition, name.end_with?('-compile'))
end

begin
  harness = {
    'exact-negative' => ['warn "FAIL contract: named condition"; exit 1', 1, 'named condition', true, false],
    'unrelated' => ['warn "FAIL contract: other condition"; exit 1', 1, 'named condition', false, false],
    'positive' => ['puts "PASS controlled mmap scenario"', 0, nil, true, false],
    'signal' => ['warn "FAIL contract: named condition"; Process.kill("KILL", Process.pid)', 1, 'named condition', false, false],
    'recovering-ubsan' => ['puts "PASS controlled mmap scenario"; warn "x.c:1: runtime error: signed integer overflow"', 0, nil, false, false],
    'asan-expected-check' => ['warn "ERROR: AddressSanitizer: heap-use-after-free"; warn "FAIL contract: named condition"; exit 1', 1, 'named condition', false, false],
    'lsan-positive' => ['puts "PASS controlled mmap scenario"; warn "ERROR: LeakSanitizer: detected memory leaks"', 0, nil, false, false],
    'hang' => ['sleep 30', 1, 'named condition', false, true],
    'held-pipe' => ['fork { sleep 30 }; exit 0', 1, 'named condition', false, true]
  }
  harness.each do |name, (code, expected, condition, accepted, must_timeout)|
    out, err, status, timed = execute.call('harness-' + name,
      [RbConfig.ruby, '-e', code], must_timeout ? 0.5 : 30)
    abort 'bad process classification: ' + name unless
      !!accepted_result?(out, err, status, timed, expected, condition) == accepted && timed == must_timeout
  end
  original = File.read(root + '/ember/tools/bcm2712-v3d-mmap-check.c')
  body = work + '/mmap-check-body.h'
  File.write(body, original)
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + uapi, '-I' + work,
      root + '/ember/tools/bcm2712-v3d-mmap-check-contract.c', '-o', work + '/contract']
  run.call('positive-compile', compile)
  scenarios = %w[pass page64 usage page-invalid open1 open2 version driver create1 create2 create3 map-ioctl1 map-ioctl2 map-ioctl3 full-map1 full-map2 full-map3 read-map write-map anon-read frame-anon copy-prefix last-byte tail-byte wrong-BO unknown-accepted unknown-errno oversize-accepted oversize-errno unowned-accepted unowned-errno interior-accepted interior-errno unmap handle-close close owner-zeroes]
  scenarios.each { |s| run.call('scenario-' + s, [work + '/contract', s]) }
  unless ENV['MMAP_SKIP_CONTROLS'] == '1'
    result = 'result == (success ? 0 : 1)'
    mutations = {
      'anonymous-read' => ['error = map_verify(bo, prefix, length, "read-prefix");', 'error = 0;', 'anon-read', result],
      'anonymous-frame' => ['error = map_verify(bo, prefix, length, "read-prefix");', 'error = 0;', 'frame-anon', result],
      'last-byte' => ['i < length; i++', 'i + 1 < length; i++', 'last-byte', result],
      'whole-tail' => ['map_verify(bo, bo->full, bo->size, "prefix-to-full-guards")', 'map_verify(bo, bo->full, length, "prefix-to-full-guards")', 'tail-byte', result],
      'negative-anon' => ['return EIO;\n\t}\n\tif (error != expected)', 'return 0;\n\t}\n\tif (error != expected)', 'unknown-accepted', result],
      'negative-errno' => ['if (error != expected)', 'if (false && error != expected)', 'unowned-errno', result],
      'unowned-permission' => ['page, EACCES, stage)', 'page, EINVAL, stage)', 'pass', result],
      'cleanup-full-map' => ['munmap((void *)bos[i].full, bos[i].size) == -1', 'false', 'pass', '!mappings[i].live'],
      'cleanup-held-map' => ['munmap((void *)held, page) == -1', 'false', 'pass', '!mappings[i].live'],
      'large-prefix' => ['i == 2 ? FRAME_BYTES : n * page', 'n * page', 'pass', 'frame_reads == 1'],
      'reverse-write' => ['bo->full[bo->changed[i]] = bo->values[i];', 'bo->full[bo->changed[i]] = 0;', 'pass', result],
      'owner-close-timing' => ['cleanup = close(fd);\n\tfd = -1;', 'cleanup = 0;', 'pass', '!owner'],
      'retained-data-read' => ['error = map_verify(&bos[i], bos[i].full, bos[i].size, stage);', 'error = 0;', 'owner-zeroes', result]
    }
    mutations.each do |name, (before, after, scenario, condition)|
      before = before.gsub('\\n', "\n").gsub('\\t', "\t")
      after = after.gsub('\\n', "\n").gsub('\\t', "\t")
      abort 'nonunique control: ' + name unless original.scan(Regexp.new(Regexp.escape(before))).size == 1
      File.write(body, original.sub(before, after))
      run.call('control-' + name + '-compile', compile)
      run.call('control-' + name, [work + '/contract', scenario], 1, condition)
      File.write(body, original)
    end
  end
  run.call('final-compile', compile)
  pins.each { |p, h| abort 'source changed: ' + p unless Digest::SHA256.file(root + '/' + p).hexdigest == h }
  puts "PASS actual-source mmap #{scenarios.length} scenarios and named controls"
ensure
  File.write(work + '/receipt.json', JSON.pretty_generate({inputs_sha256:pins, commands:commands,
    scope:'Actual checker body with controlled host allocation/mapping services. No native kernel, GPU, DMA or hardware acceptance.'}) + "\n")
  puts 'Evidence: ' + work
  FileUtils.remove_entry(work) unless evidence
end
