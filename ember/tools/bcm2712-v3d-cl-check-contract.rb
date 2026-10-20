#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual CL checker with controlled host DRM services.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'optparse'
require 'shellwords'
require 'tmpdir'
require 'timeout'

Process.setrlimit(:CORE, 0, 0)

def require_check(condition, message)
  raise message unless condition
end

def capture(command, seconds = 60)
  Open3.popen3(*command, pgroup: true) do |input, output, errors, waiter|
    input.close
    stdout = Thread.new { output.read }
    stderr = Thread.new { errors.read }
    begin
      return Timeout.timeout(seconds) { [stdout.value, stderr.value, waiter.value] }
    rescue Timeout::Error
      begin
        Process.kill('KILL', -waiter.pid)
      rescue Errno::ESRCH
        # The process can exit at the timeout boundary.
      end
      waiter.value
      raise "command timed out: #{command.shelljoin}"
    ensure
      stdout.join
      stderr.join
    end
  end
end

def run(command, expected = 0)
  stdout, stderr, status = capture(command)
  require_check(status.exited? && status.exitstatus == expected,
                "#{command.shelljoin}: #{status.inspect}, expected #{expected}\n#{stdout}#{stderr}")
  [stdout, stderr, status]
end

options = { source: File.expand_path('../..', __dir__), cc: ENV.fetch('CC', 'cc'),
            cflags: ENV.fetch('CFLAGS', '') }
OptionParser.new do |parser|
  parser.banner = 'Usage: bcm2712-v3d-cl-check-contract.rb [options]'
  parser.on('--source PATH') { |value| options[:source] = File.expand_path(value) }
  parser.on('--work PATH') { |value| options[:work] = File.expand_path(value) }
  parser.on('--cc COMMAND') { |value| options[:cc] = value }
  parser.on('--cflags FLAGS') { |value| options[:cflags] = value }
end.parse!
require_check(ARGV.empty?, 'unexpected arguments')
src = options[:source]
paths = {
  'checker' => "#{src}/ember/tools/bcm2712-v3d-cl-check.c",
  'helpers' => "#{src}/ember/tools/bcm2712-v3d-render-check.c",
  'packets' => "#{src}/sys/arch/arm/broadcom/bcm2712_v3d_queue.c",
  'fixture' => "#{src}/ember/tools/bcm2712-v3d-cl-check-contract.c",
  'runner' => File.realpath(__FILE__)
}
uapi = "#{src}/sys/external/bsd/drm2/dist/include/uapi/drm"
%w[drm.h drm_mode.h v3d_drm.h].each { |name| paths[name] = "#{uapi}/#{name}" }
pin_inputs = -> { paths.transform_values { |path| Digest::SHA256.file(path).hexdigest } }
pins = pin_inputs.call
if options[:work]
  work = options[:work]
  require_check(!File.exist?(work), 'work directory already exists')
  FileUtils.mkdir_p(work)
else
  work = Dir.mktmpdir('v3d-cl-check-', ENV['TMPDIR'])
end
begin
  packet_source = File.read(paths.fetch('packets'))
  definitions = packet_source.lines.grep(/^#define QV3D_/).join
  first = packet_source.index('struct qv3d_cl {')
  last = first && packet_source.index('/* --- bus_dma objects', first)
  require_check(first && last, 'packet source boundaries changed')
  File.write("#{work}/cl-packets.h", definitions + packet_source[first...last])
  original = File.read(paths.fetch('checker'))
  marker = "#include \"cl-packets.h\"\n"
  require_check(original.scan(marker).length == 1, 'packet include boundary changed')
  command = Shellwords.split(options[:cc]) + %w[-std=c11 -Wall -Wextra -Werror -Wno-unused-function] +
            Shellwords.split(options[:cflags]) + ["-I#{uapi}", "-I#{src}/ember/tools",
                                                 "-I#{work}", paths.fetch('fixture'),
                                                 '-o', "#{work}/contract"]
  compile_subject = lambda do |text|
    # The production file already renames the imported helper main.
    File.write("#{work}/checker-body.h", text.sub(marker, marker + "#define main cl_check_main\n"))
    run(command)
  end
  checks = 0
  compile_subject.call(original)
  stdout, stderr, = run(["#{work}/contract", 'pass'])
  File.write("#{work}/positive.log", stdout + stderr)
  checks += 1
  failures = {
    'cap64' => 'SUBMIT_CL round 0', 'partial' => 'CREATE_BO',
    'duplicate' => 'CL duplicate BO handle', 'overlap' => 'CL overlapping BO addresses',
    'wrap' => 'CL BO address wraps', 'submit' => 'SUBMIT_CL round 0',
    'wait' => 'CL fence not retired', 'close' => 'GEM_CLOSE',
    'pixel' => 'expected', 'output-guard' => 'expected',
    'command' => 'CL modified commands/guard', 'tile-alloc' => 'CL tile allocation guard',
    'tile-state' => 'CL tile state guard', 'unused' => 'CL wrote unrelated admitted BO',
    'fd-close' => 'CL close', 'unmap' => 'CL munmap'
  }
  failures.each do |mode, diagnostic|
    stdout, stderr, = run(["#{work}/contract", mode], 1)
    require_check(stderr.include?(diagnostic), "missing #{mode} diagnostic")
    File.write("#{work}/#{mode}.log", stdout + stderr)
    checks += 1
  end
  controls = {
    'omit72' => ['render(argv[1], 4, 72);', 'render(argv[1], 4, 5);', 'pass'],
    'omit91' => ['render(argv[1], 4, 91);', 'render(argv[1], 4, 5);', 'pass'],
    'short72' => ['render(argv[1], 4, 72);', 'render(argv[1], 3, 72);', 'pass'],
    'short91' => ['render(argv[1], 4, 91);', 'render(argv[1], 3, 91);', 'pass'],
    'wrong-count' => ['.bo_handle_count = count,', '.bo_handle_count = 5,', 'pass'],
    'omit-close' => ['close_handle(fd, bo[i].handle);', '(void)fd;', 'pass'],
    'omit-unmap' => ['check(munmap(bo[i].map, BO_SIZE) == 0, "CL munmap");', '(void)bo[i].map;', 'pass'],
    'same-guard' => ['GUARD ^ (bo_index - 4) * UINT32_C(0x01020305)', 'GUARD ^ (bo_index - bo_index)', 'pass'],
    'omit-unused-check' => ['check(bo[b].map[i] == cl_guard(b),', 'check(true || bo[b].map[i] == cl_guard(b),', 'unused'],
    'omit-unique-check' => ['check(handles[i] != handles[j],', 'check(true || handles[i] != handles[j],', 'duplicate'],
    'omit-wrap-check' => ['check((uint64_t)bo[i].gpu + BO_SIZE <= UINT64_C(0x100000000),',
                          'check(true || (uint64_t)bo[i].gpu + BO_SIZE <= UINT64_C(0x100000000),', 'wrap']
  }
  controls.each do |name, (old, replacement, mode)|
    require_check(original.scan(Regexp.new(Regexp.escape(old))).length == 1,
                  "control boundary changed: #{name}")
    compile_subject.call(original.sub(old, replacement))
    expected, condition_marker = {
      'omit72' => [1, 'atomic_load(&totals->list72) == 4'],
      'omit91' => [1, 'atomic_load(&totals->list91) == 4'],
      'short72' => [1, 'atomic_load(&totals->cl) == 333'],
      'short91' => [1, 'atomic_load(&totals->cl) == 333'],
      'wrong-count' => [1, 'open_bos == count && a->flags == 0 && a->pad == 0'],
      'omit-close' => [1, 'bo->map_live && !bo->handle_live'],
      'omit-unmap' => [1, '!bos[i].handle_live && !bos[i].map_live'],
      'same-guard' => [1, 'bo[b]->words[i] =='],
      'omit-unused-check' => [99, 'injected failure was not detected'],
      'omit-unique-check' => [1, 'handles[i] != handles[j]'],
      'omit-wrap-check' => [99, 'injected failure was not detected']
    }.fetch(name)
    stdout, stderr, = run(["#{work}/contract", mode], expected)
    require_check(stderr.include?(condition_marker), "missing causal condition: #{name}")
    require_check(stderr.include?('FAIL contract:') || expected == 99,
                  "missing controlled causal failure: #{name}")
    File.write("#{work}/control-#{name}.log", stdout + stderr)
    checks += 1
  end
  compile_subject.call(original)
  require_check(pins == pin_inputs.call, 'source inputs changed during the contract')
  receipt = { checks: checks, source_pins: pins, compile: command,
              scope: 'Host checker and caller adapters. No GPU execution.' }
  File.write("#{work}/receipt.json", JSON.pretty_generate(receipt) + "\n")
  puts "PASS: #{checks} actual-source CL checker scenarios and causal controls"
ensure
  FileUtils.remove_entry(work) unless options[:work]
end
