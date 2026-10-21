#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual TFU raster checker with controlled host DRM services.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'optparse'
require 'shellwords'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
require_relative 'bcm2712-v3d-tfu-oracle'

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
  require_check(!(stdout + stderr).match?(/AddressSanitizer:|UndefinedBehaviorSanitizer:|LeakSanitizer:|runtime error:/),
                "sanitizer diagnostic: #{command.shelljoin}\n#{stdout}#{stderr}")
  require_check(status.exited? && status.exitstatus == expected,
                "#{command.shelljoin}: #{status.inspect}, expected #{expected}\n#{stdout}#{stderr}")
  [stdout, stderr, status]
end

options = { source: File.expand_path('../..', __dir__), cc: ENV.fetch('CC', 'cc'),
            cflags: ENV.fetch('CFLAGS', '') }
OptionParser.new do |parser|
  parser.banner = 'Usage: bcm2712-v3d-tfu-check-contract.rb [options]'
  parser.on('--source PATH') { |value| options[:source] = File.expand_path(value) }
  parser.on('--mesa-source PATH') { |value| options[:mesa] = File.expand_path(value) }
  parser.on('--work PATH') { |value| options[:work] = File.expand_path(value) }
  parser.on('--cc COMMAND') { |value| options[:cc] = value }
  parser.on('--cflags FLAGS') { |value| options[:cflags] = value }
end.parse!
require_check(ARGV.empty?, 'unexpected arguments')
require_check(options[:mesa], '--mesa-source is required; use pinned Mesa 26.2.4 source or upstream archive')
src = options[:source]
paths = {
  'checker' => "#{src}/ember/tools/bcm2712-v3d-tfu-check.c",
  'helpers' => "#{src}/ember/tools/bcm2712-v3d-render-check.c",
  'fixture' => "#{src}/ember/tools/bcm2712-v3d-tfu-check-contract.c",
  'runner' => File.realpath(__FILE__),
  'oracle_generator' => File.join(__dir__, 'bcm2712-v3d-tfu-oracle.rb')
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
  work = Dir.mktmpdir('v3d-tfu-check-', ENV['TMPDIR'])
end
begin
  oracle = TfuOracle.prepare(options[:mesa], "#{work}/mesa-tfu-oracle.h")
  oracle_text = File.read("#{work}/mesa-tfu-oracle.h")
  model_oracle = oracle_text.gsub('EMBER_TFU_MESA_ORACLE', 'EMBER_TFU_MESA_ORACLE_MODEL')
  TfuOracle::FUNCTIONS.each_key do |name|
    model_oracle = model_oracle.gsub(/\b#{Regexp.escape(name)}\b/, "model_#{name}")
  end
  File.write("#{work}/model-mesa-tfu-oracle.h", model_oracle)
  original =
 File.read(paths.fetch('checker'))
  helpers = File.read(paths.fetch('helpers'))
  marker = "#include <time.h>\n"
  require_check(original.scan(marker).length == 1, 'main rename boundary changed')
  command = Shellwords.split(options[:cc]) + %w[-std=c11 -Wall -Wextra -Werror -Wno-unused-function] +
            Shellwords.split(options[:cflags]) + ["-I#{uapi}", "-I#{work}", paths.fetch('fixture'),
                                                 '-o', "#{work}/contract"]
  compile_subject = lambda do |text, helper_text = helpers, oracle_override = oracle_text|
    File.write("#{work}/mesa-tfu-oracle.h", oracle_override)
    File.write("#{work}/checker-body.h", text.sub(marker, marker + "#define main tfu_check_main\n"))
    File.write("#{work}/bcm2712-v3d-render-check.c", helper_text)
    run(command)
  end
  checks = 0
  generator = [RbConfig.ruby, paths.fetch('oracle_generator')]
  bad = "#{work}/bad-mesa"
  FileUtils.mkdir_p(bad)
  _, _, mesa_original = TfuOracle.read_sources(options[:mesa])
  mesa_original.each do |path, text|
    full = File.join(bad, path)
    FileUtils.mkdir_p(File.dirname(full))
    File.write(full, text + "\n/* altered source */\n")
  end
  output = "#{work}/rejected-oracle.h"
  stdout, stderr, = run([*generator, bad, output], 1)
  require_check(stderr.include?('FAIL: Mesa source SHA256 mismatch:'), 'source drift was accepted')
  require_check(!File.exist?(output), 'rejected source published a header')
  File.write("#{work}/generator-source-drift.log", stdout + stderr)
  checks += 1
  File.write("#{work}/bad-archive", 'not the pinned archive')
  stdout, stderr, = run([*generator, "#{work}/bad-archive", output], 1)
  require_check(stderr.include?('FAIL: Mesa archive SHA256 mismatch'), 'wrong archive was accepted')
  require_check(!File.exist?(output), 'rejected archive published a header')
  File.write("#{work}/generator-archive-drift.log", stdout + stderr)
  checks += 1
  stdout, stderr, = run([*generator, options[:mesa], "#{work}/mesa-tfu-oracle.h"], 1)
  require_check(stderr.include?('FAIL: Oracle output already exists'), 'existing output was overwritten')
  require_check(File.read("#{work}/mesa-tfu-oracle.h") == oracle_text, 'existing oracle changed')
  File.write("#{work}/generator-existing-output.log", stdout + stderr)
  checks += 1
  boundary = 'begin; TfuOracle.function("uint32_t\nfake()\n{", "fake", "uint32_t\nfake()\n{"); '
  boundary += 'rescue TfuOracle::Error => e; warn "FAIL: #{e.message}"; exit 1; end; exit 99'
  stdout, stderr, = run([RbConfig.ruby, '-r', paths.fetch('oracle_generator'), '-e', boundary], 1)
  require_check(stderr.include?('FAIL: Mesa function has no closing boundary: fake'), 'truncated extraction was accepted')
  File.write("#{work}/generator-boundary.log", stdout + stderr)
  checks += 1
  compile_subject.call(original)

  %w[pass padding legacy].each do |mode|
    stdout, stderr, = run(["#{work}/contract", mode])
    File.write("#{work}/#{mode}.log", stdout + stderr)
    checks += 1
  end
  failures = {
    'old-ioc' => 'SUBMIT_TFU 64x64 stride64 IOC0 round0',
    'open' => 'open TFU node', 'create1' => 'CREATE_BO', 'create2' => 'CREATE_BO',
    'map' => 'mmap GEM', 'zero' => 'new GEM allocation is not zeroed',
    'mmap-ioctl' => 'MMAP_BO', 'get-offset' => 'GET_BO_OFFSET',
    'wrong-offset' => 'inconsistent GPU address', 'sync-create' => 'TFU SYNCOBJ_CREATE',
    'clock' => 'TFU clock_gettime', 'sync-wait' => 'TFU syncobj wait',
    'bo-wait' => 'TFU WAIT_BO', 'gem-close' => 'GEM_CLOSE',
    'unmap' => 'TFU source munmap', 'fd-close' => 'TFU close',
    'submit' => 'SUBMIT_TFU', 'source' => 'TFU modified source',
    'pixel' => 'TFU copy/guard', 'guard' => 'TFU copy/guard',
    'short-map' => 'TFU copy/guard job8', 'no-copy' => 'TFU copy/guard job0',
    'oracle-memory' => 'TFU tiled oracle allocation',
    'tiled-no-copy' => 'TFU tiled copy/guard job16',
    'reverse-no-copy' => 'TFU tiled copy/guard job20',
    'tiled-memcpy' => 'TFU tiled copy/guard job16',
    'forward-memcpy' => 'TFU tiled copy/guard job16',
    'tiled-source' => 'TFU tiled modified source',
    'tiled-guard' => 'TFU tiled copy/guard job16'
  }
  failures.each do |mode, diagnostic|
    stdout, stderr, = run(["#{work}/contract", mode], 1)
    require_check(stderr.include?(diagnostic), "missing #{mode} diagnostic")
    File.write("#{work}/#{mode}.log", stdout + stderr)
    checks += 1
  end
  controls = {
    'omit-ioc-zero' => [', 0, &jobs);', ', 1, &jobs);', 'pass', 1,
                       'submitted < 16 && a->icfg == ((29U << 16) | ioc)'],
    'omit-large-case' => ['sizeof(raster_cases) / sizeof(raster_cases[0])', '1', 'pass', 1, 'submitted < 16 && a->icfg == ((29U << 16) | ioc)'],
    'omit-source-offset' => ['{84, 52, 96, 256, 512}', '{84, 52, 96, 0, 512}', 'pass', 1,
                            'a->iia == src->gpu + source && a->ioa == dst->gpu + destination'],
    'omit-guard-check' => ['if (dst->map[word] != expected)', 'if (false && dst->map[word] != expected)',
                          'guard', 99, 'FAIL: injected failure was not detected'],
    'omit-source-check' => ['check(src->map[word] == pattern(iteration, word),',
                           'check(true || src->map[word] == pattern(iteration, word),',
                           'source', 99, 'FAIL: injected failure was not detected'],
    'omit-fence' => ['raster_fence(fd, sync_handle);', '(void)sync_handle;', 'pass', 1,
                    'fence_waits == submitted && bo_waits < submitted * 2'],
    'omit-second-wait' => ['b < 2;', 'b < 1;', 'pass', 1,
                          'fence_waits == submitted && bo_waits == submitted * 2'],
    'omit-close' => ['close_handle(fd, src.handle);', '(void)src;', 'pass', 1,
                     'length == bo->size && bo->map_live && !bo->handle_live'],
    'omit-unmap' => ['check(munmap(src.map, TFU_BO_SIZE) == 0,',
                     'check(true || munmap(src.map, TFU_BO_SIZE) == 0,', 'pass', 1,
                     'fd == 77 && fd_live && closed == created && unmapped == mapped'],
    'omit-reverse-refill' => ['src->map[input / 4] = value;',
                              'if (direction == 0) src->map[input / 4] = value;',
                              'pass', 1, 'src->data[from] == pattern(submitted, y * 64 + x)'],
    'omit-tiled-cases' => ['tiled_test(fd, &src, &dst, sync_create.handle, &jobs);',
                           '(void)jobs;', 'pass', 1, 'TFU job count'],
    'omit-tiled-source-check' => ['check(src->map[word] == source_snapshot[word],',
                                'check(true || src->map[word] == source_snapshot[word],',
                                'tiled-source', 99, 'FAIL: injected failure was not detected'],
    'omit-tiled-copy-check' => ['if (dst->map[word] != expected[word])',
                              'if (false && dst->map[word] != expected[word])',
                              'tiled-guard', 99, 'FAIL: injected failure was not detected'],
    'omit-intermediate-check' => ['if (dst->map[word] != expected[word])',
                                'if (direction && dst->map[word] != expected[word])',
                                'forward-memcpy', 99, 'FAIL: injected failure was not detected'],
    'padding-as-pixels' => ['if (offset % c->stride >= c->width)',
                           'if (false && offset % c->stride >= c->width)', 'padding', 1, 'TFU copy/guard job8']
  }
  controls.each do |name, (old, replacement, mode, expected, condition)|
    require_check(original.scan(Regexp.new(Regexp.escape(old))).length == 1,
                  "control boundary changed: #{name}")
    compile_subject.call(original.sub(old, replacement))
    stdout, stderr, = run(["#{work}/contract", mode], expected)
    File.write("#{work}/control-#{name}.log", stdout + stderr)
    require_check(stderr.include?(condition), "missing causal condition: #{name}: #{stderr}")
    checks += 1
  end
  # Model-side CPU helpers stay byte-identical when the caller oracle changes.
  old_offset = "return v3d_get_uif_pixel_offset(cpp, image_h, x, y, false);"
  require_check(oracle_text.scan(old_offset).length == 1, 'oracle offset boundary changed')
  compile_subject.call(original, helpers, oracle_text.sub(old_offset, '(void)image_h; return (y * 64 + x) * cpp;'))
  stdout, stderr, = run(["#{work}/contract", 'pass'], 1)
  require_check(stderr.include?('TFU tiled copy/guard job16'), 'linear oracle control survived')
  File.write("#{work}/control-linear-oracle.log", stdout + stderr)
  checks += 1
  old = 'return allocate_bytes(fd, BO_SIZE);'
  require_check(helpers.scan(Regexp.new(Regexp.escape(old))).length == 1, 'legacy wrapper boundary changed')
  compile_subject.call(original, helpers.sub(old, 'return allocate_bytes(fd, 24576);'))
  stdout, stderr, = run(["#{work}/contract", 'legacy'], 1)
  require_check(stderr.include?('created < 2 && a->size == size && a->flags == 0'), 'legacy size control survived')
  File.write("#{work}/control-legacy-size.log", stdout + stderr)
  checks += 1
  compile_subject.call(original)
  _, _, mesa_after = TfuOracle.read_sources(options[:mesa])
  require_check(mesa_after.all? { |path, text| Digest::SHA256.hexdigest(text) == oracle.fetch(:source_pins).fetch(path) },
                'Mesa inputs changed during contract')
  require_check(pins == pin_inputs.call, 'source inputs changed during the contract')
  receipt = { checks: checks, source_pins: pins, oracle: oracle, compile: command,
              scope: 'Actual raster/tiled checker, independent pinned Mesa CPU oracle and DRM adapters. No GPU execution or mip acceptance.' }
  File.write("#{work}/receipt.json", JSON.pretty_generate(receipt) + "\n")
  puts "PASS: #{checks} actual-source TFU raster/tiled scenarios and causal controls"
ensure
  FileUtils.remove_entry(work) unless options[:work]
end
