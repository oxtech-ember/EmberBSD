#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
require 'timeout'
require 'rbconfig'
Process.setrlimit(:CORE, 0, 0)

# Use the CL checker runner's group and reader deadline.
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


# A recovering sanitizer or a later CHECK must never turn a memory error green.
def sanitizer_diagnostic?(out, err)
  (out + err).match?(/AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|runtime error:/)
end

# Exercise the common status/deadline gate without invoking a compiler.
def process_controls(dir)
  records = []
  {
    'success' => ['exit 0', :success],
    'signal' => ['Process.kill("KILL", Process.pid)', :refuse],
    'UBSan-recover' => ['warn "runtime error: controlled recover"; exit 0', :refuse],
    'ASan-then-CHECK' => ['warn "AddressSanitizer: controlled"; warn "FAIL line 1: named condition"; exit 1', :refuse],
    'inherited-pipe' => ['fork { sleep 30 }; exit 0', :timeout]
  }.each do |name, (code, expected)|
    command = [RbConfig.ruby, '-e', code]
    out = err = ''; status = nil; timed_out = false
    begin
      out, err, status = capture(command, expected == :timeout ? 1 : 30)
    rescue RuntimeError => exception
      raise unless exception.message.start_with?('command timed out:')
      timed_out = true; err = exception.message + "\n"
    end
    File.write(dir + '/process-' + name + '.stdout', out)
    File.write(dir + '/process-' + name + '.stderr', err)
    records << {name: name, argv: command, exit: status && status.exitstatus,
      signal: status && status.termsig, timeout: timed_out, sanitizer: sanitizer_diagnostic?(out, err)}
    positive = status && status.success? && !sanitizer_diagnostic?(out, err)
    causal = status && status.exited? && status.exitstatus == 1 &&
      err.include?('FAIL line 1: named condition') && !sanitizer_diagnostic?(out, err)
    abort 'Process gate accepted ' + name if expected != :success && (positive || causal)
    abort 'Process gate refused success' if expected == :success && !positive
    abort 'Process deadline failed' unless timed_out == (expected == :timeout)
    puts 'PASS host process control ' + name
  end
  File.write(dir + '/process-controls.json', JSON.pretty_generate(records) + "\n")
end

# Extract definitions with balanced lexical braces, not a greedy function regexp.
def block_end(text, first)
  depth = 0; state = :code; i = first
  while i < text.size
    c = text[i]; pair = text[i, 2]
    case state
    when :line
      state = :code if c == "\n"
    when :comment
      if pair == '*/'; state = :code; i += 1; end
    when :string, :character
      if c == '\\'; i += 1
      elsif (state == :string && c == '"') || (state == :character && c == "'")
        state = :code
      end
    else
      if pair == '//'; state = :line; i += 1
      elsif pair == '/*'; state = :comment; i += 1
      elsif c == '"'; state = :string
      elsif c == "'"; state = :character
      elsif c == '{'; depth += 1
      elsif c == '}'
        depth -= 1
        return i + 1 if depth.zero?
      end
    end
    i += 1
  end
  abort 'Unterminated source definition'
end

def function(text, name)
  name_pos = text.index(/^#{Regexp.escape(name)}\(/) ||
    text.index(/^static[^\n]*\b#{Regexp.escape(name)}\(/)
  abort 'Missing function: ' + name unless name_pos
  first = text.rindex("\n", name_pos - 2) + 1
  brace = text.index('{', name_pos)
  abort 'Missing static function boundary: ' + name unless first && brace
  last = block_end(text, brace)
  { name: name, text: text[first...last] + "\n", line: text[0...first].count("\n") + 1 }
end

def definition(text, name, pattern)
  first = text.index(pattern)
  abort 'Missing definition: ' + name unless first
  last = block_end(text, text.index('{', first))
  abort 'Missing definition terminator: ' + name unless text[last] == ';'
  { name: name, text: text[first..last] + "\n", line: text[0...first].count("\n") + 1 }
end

root = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['TFU_REJECT_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('v3d-tfu-reject-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(dir)
inputs = {}; commands = []; fragments = []
read = lambda do |path|
  text = File.binread(root + '/' + path)
  inputs[path] = Digest::SHA256.hexdigest(text)
  text
end
begin
  native = 'sys/external/bsd/drm2/bcmv3d/'
  job = read.call(native + 'bcmv3d_job.c')
  header = read.call(native + 'bcmv3d_tfu_reject.h')
  uapi = read.call('sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h')
  %w[c rb].each { |ext| read.call('ember/tools/bcmv3d-tfu-reject-contract.' + ext) }
  constants = %w[V3D_MSK_SET V3D_IRQ_ALL V3D_COPY_SIZE V3D_TFU_IOC V3D_TFU_MESA_ICFG V3D_TFU_MESA_IOC V3D_TFU_MESA_MAX_DIM].map do |name|
    job[/^#define #{name}\s+[^\n]+/] || abort('Missing native constant ' + name)
  end.join("\n") + "\n"
  types = definition(uapi, 'submit-tfu-uapi', /^struct drm_v3d_submit_tfu \{/)[:text]
  %w[bcmv3d_job bcmv3d_sync bcmv3d_tfu_reject_bo].each do |name|
    fragment = definition(job, name, /^struct #{name} \{/)
    fragments << fragment.reject { |key, _| key == :text }; types += fragment[:text]
  end
  names = %w[bcmv3d_payload bcmv3d_tfu_reject_snapshot bcmv3d_tfu_reject_take bcmv3d_tfu_reject bcmv3d_tfu_lookup bcmv3d_tfu_address bcmv3d_submit_tfu]
  parts = {}; body = names.map do |name|
    fragment = function(job, name)
    fragments << fragment.reject { |key, _| key == :text }; parts[name] = fragment[:text]
  end.join("\n")
  File.write(dir + '/tfu-types.h', header + constants + types)
  File.write(dir + '/tfu-body.h', body)
  File.write(dir + '/source-job.c', job)
  run = lambda do |name, argv, required = true, seconds = 60|
    out = ''; err = ''; status = nil; timeout = false
    begin
      out, err, status = capture(argv, seconds)
    rescue RuntimeError => exception
      raise unless exception.message.start_with?('command timed out:')
      timeout = true; err = exception.message + "\n"
    end
    File.write(dir + '/' + name + '.stdout', out)
    File.write(dir + '/' + name + '.stderr', err)
    commands << { name: name, argv: argv, exit: status && status.exitstatus,
      signal: status && status.termsig, timeout: timeout }
    abort name + ': sanitizer diagnostic' if sanitizer_diagnostic?(out, err)
    abort name + ': inspect retained stderr' if required && (timeout || !status.success?)
    [out, err, status, timeout]
  end
  causal = lambda do |err, status, timeout, condition|
    !timeout && status && status.exited? && status.exitstatus == 1 &&
      err.lines.any? { |line| line.match?(/^FAIL line \d+: /) &&
        line.sub(/^FAIL line \d+: /, '').strip == condition }
  end
  process_controls(dir) unless ENV['TFU_REJECT_SKIP_HARNESS'] == '1'
  unless ENV['TFU_REJECT_SKIP_HARNESS'] == '1'
    harness = {
      'exact' => ['warn "FAIL line 1: named condition"; exit 1', true, false],
      'unrelated' => ['warn "FAIL line 1: another condition"; exit 1', false, false],
      'success' => ['warn "FAIL line 1: named condition"; exit 0', false, false],
      'signal' => ['warn "FAIL line 1: named condition"; Process.kill("KILL", Process.pid)', false, false],
      'hang' => ['sleep 30', false, true],
      'held-pipe' => ['fork { sleep 30 }; exit 0', false, true]
    }
    harness.each do |name, (code, accepted, must_timeout)|
      _, err, status, timeout = run.call('harness-' + name,
        [RbConfig.ruby, '-e', code], false, must_timeout ? 1 : 30)
      abort 'Invalid harness classification ' + name unless
        !!causal.call(err, status, timeout, 'named condition') == accepted && timeout == must_timeout
      puts 'PASS host process control ' + name
    end
  end
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) +
    %w[-std=c11 -Wall -Wextra -Werror -pthread] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + dir,
    root + '/ember/tools/bcmv3d-tfu-reject-contract.c', '-o', dir + '/contract']
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
  run.call('positive-compile', compile)
  out, = run.call('positive', execute + [compile[-1]]); print out
  unless ENV['TFU_REJECT_SKIP_MUTANTS'] == '1'
    controls = {
      'saturating-ceiling' => ['bcmv3d_tfu_reject_take', 'before >= BCMV3D_TFU_REJECT_MAX', 'before > BCMV3D_TFU_REJECT_MAX'],
      'counter-per-device' => ['bcmv3d_tfu_reject_take', 'before = atomic_load_relaxed(&job->tfu_rejections);', '(void)job; static unsigned int shared; before = atomic_load_relaxed(&shared);'],
      'CAS-ticket-ownership' => ['bcmv3d_tfu_reject_take', 'before + 1) ==', 'before + 1) !='],
      'all-admission-errno' => ['bcmv3d_tfu_reject', 'error == 0 ||', 'error != EINVAL ||'],
      'no-success-receipt' => ['bcmv3d_tfu_reject', 'error == 0 ||', 'false ||'],
      'valid-reason-only' => ['bcmv3d_tfu_reject', 'reason <= BCMV3D_TFU_REJECT_NONE', 'reason < BCMV3D_TFU_REJECT_NONE'],
      'saved-handle-count' => ['bcmv3d_tfu_reject', 'error, count, a->flags', 'error, 2U + 0U * count, a->flags'],
      'extension-presence' => ['bcmv3d_tfu_reject', '(unsigned int)(a->extensions != 0)', '(unsigned int)a->extensions'],
      'all-coefficients' => ['bcmv3d_tfu_reject', 'a->coef[1], a->coef[2], a->coef[3]', 'a->coef[1], a->coef[1], a->coef[3]'],
      'known-only-after-lookup' => ['bcmv3d_tfu_reject_snapshot', 'info->known = true;', 'info->known = false;'],
      'snapshot-size' => ['bcmv3d_tfu_reject_snapshot', 'info->bytes = bo->cma.dmasize;', 'info->bytes = V3D_COPY_SIZE;'],
      'snapshot-gpu-base' => ['bcmv3d_tfu_reject_snapshot', 'info->gpu = bo->gpu;', 'info->gpu = 0;'],
      'source-lookup-reason' => ['bcmv3d_tfu_lookup', 'BCMV3D_TFU_REJECT_SRC_LOOKUP;', 'BCMV3D_TFU_REJECT_DST_LOOKUP;'],
      'post-put-snapshot' => ['bcmv3d_submit_tfu', "\tbcmv3d_tfu_reject(sc, args, reason, error, &source, &destination);", "\tif (dst != NULL) bcmv3d_tfu_reject_snapshot(&destination, dst);\n\tbcmv3d_tfu_reject(sc, args, reason, error, &source, &destination);"],
      'post-unwind-only' => ['bcmv3d_submit_tfu', "release:\n\tbcmv3d_resv_unlock", "release:\n\tif (count != 0) bcmv3d_tfu_reject(sc, args, reason, error, &source, &destination);\n\tbcmv3d_resv_unlock"],
      'execution-not-admission' => ['bcmv3d_submit_tfu', "\t\tbcmv3d_sync_fini(&sync);\n\t\treturn -error;", "\t\tbcmv3d_sync_fini(&sync);\n\t\tbcmv3d_tfu_reject(sc, args, BCMV3D_TFU_REJECT_PAYLOAD, error, &source, &destination);\n\t\treturn -error;"],
      'reference-release' => ['bcmv3d_submit_tfu', 'drm_gem_object_put_unlocked(objects[--count]);', '(void)objects[--count];'],
      'original-errno' => ['bcmv3d_submit_tfu', "\treturn -error;\n}", "\treturn error;\n}"]
    }
    expected_checks = {
      'saturating-ceiling' => '!bcmv3d_tfu_reject_take(&a, &sample)',
      'counter-per-device' => 'a.tfu_rejections == 8 && sample == 8 && a.cl_rejections == UINT_MAX',
      'CAS-ticket-ownership' => 'bcmv3d_tfu_reject_take(&a, &sample)',
      'all-admission-errno' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'no-success-receipt' => 'env.receipts == 0 && env.job.tfu_rejections == 0',
      'valid-reason-only' => 'env.receipts == 0 && env.job.tfu_rejections == 0',
      'saved-handle-count' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'extension-presence' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'all-coefficients' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'known-only-after-lookup' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'snapshot-size' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'snapshot-gpu-base' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'source-lookup-reason' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'post-put-snapshot' => 'env.receipts == 1 && strcmp(env.logs[0], expected) == 0',
      'post-unwind-only' => 'env.bo[i].gem.refs == 1',
      'execution-not-admission' => 'env.bo[i].gem.refs == 1',
      'reference-release' => 'env.executes == 1 && env.bo[0].gem.refs == 1 && env.bo[1].gem.refs == 1',
      'original-errno' => 'bcmv3d_submit_tfu(&env.dev, &env.args, &env.file) == -error'
    }
    controls.each do |name, (part, before, after)|
      text = parts.fetch(part)
      expected = 1
      abort 'Nonunique control ' + name unless text.scan(before).size == expected
      changed = text.sub(before) { after }
      mutated = body.sub(text) { changed }
      if name == 'counter-per-device'
        mutated = mutated.sub('atomic_cas_uint(&job->tfu_rejections, before, before + 1)',
          'atomic_cas_uint(&shared, before, before + 1)')
      end
      File.write(dir + '/tfu-body.h', mutated)
      run.call(name + '-compile', compile)
      _, err, status, timeout = run.call(name, execute + [compile[-1]], false)
      abort 'Noncausal control ' + name unless causal.call(err, status, timeout, expected_checks.fetch(name))
      puts 'PASS compiled FAIL control ' + name
      File.write(dir + '/tfu-body.h', body)
    end
  end
  run.call('final-compile', compile)
  inputs.each do |path, hash|
    abort 'Input changed: ' + path unless Digest::SHA256.file(root + '/' + path).hexdigest == hash
  end
ensure
  File.write(dir + '/receipt.json', JSON.pretty_generate({ inputs_sha256: inputs,
    fragments: fragments, commands: commands, limitations: [
      'Actual TFU UAPI, private job/sync/receipt structures, payload, snapshot, counter, logger and submission.',
      'Controlled kernel/DRM, sync, reservations and execution; no GPU, MMIO, hardware or scheduler acceptance.',
      'Injected helper errors prove routing, not native helper reachability. Metadata poisoning after put checks that printing uses the earlier snapshot.'
    ] }) + "\n")
  puts 'Evidence: ' + dir
  FileUtils.remove_entry(dir) unless evidence
end
