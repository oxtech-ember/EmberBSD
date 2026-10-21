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
evidence = ENV['PTES_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('v3d-ptes-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(dir)
inputs = {}; commands = []
begin
  native = root + '/sys/external/bsd/drm2/bcmv3d/bcmv3d_job.c'
  job = File.binread(native)
  inputs[native] = Digest::SHA256.hexdigest(job)
  %w[c rb].each { |ext| p = root + '/ember/tools/bcmv3d-ptes-contract.' + ext; inputs[p] = Digest::SHA256.file(p).hexdigest }
  constants = %w[V3D_COPY_SIZE V3D_PTE_VALID V3D_PTE_WRITE].map { |n| job[/^#define #{n}\s+[^\n]+/] || abort(n) }.join("\n")
  types = definition(job, 'job', /^struct bcmv3d_job \{/)[:text]
  types = "#define PTE_TFU_VECTOR\n" + types if types.include?('tfu_bos[')
  body = function(job, 'bcmv3d_ptes')[:text]
  File.write(dir + '/ptes-types.h', constants + "\n" + types)
  File.write(dir + '/ptes-body.h', body)
  compile = lambda do |page|
    Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] +
      Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-DPAGE_SIZE=' + page.to_s, '-I' + dir,
      root + '/ember/tools/bcmv3d-ptes-contract.c', '-o', dir + '/contract']
  end
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [dir + '/contract']
  run = lambda do |name, argv, required = true|
    out, err, status = capture(argv)
    File.write(dir + '/' + name + '.stdout', out); File.write(dir + '/' + name + '.stderr', err)
    commands << { name: name, argv: argv, exit: status.exitstatus, signal: status.termsig }
    abort name + ': sanitizer diagnostic' if sanitizer_diagnostic?(out, err)
    abort name unless !required || status.success?
    [out, err, status]
  end
  process_controls(dir) unless ENV['PTES_SKIP_HARNESS'] == '1'
  [4096, 65536].each do |page|
    run.call('compile-' + page.to_s, compile.call(page))
    out, = run.call('positive-' + page.to_s, execute); print out
  end
  unless ENV['PTES_SKIP_MUTANTS'] == '1'
    controls = {
      'GPU-granule-step' => [65536, 'offset += 4096', 'offset += PAGE_SIZE'],
      'GPU-granule-PFN' => [65536, 'offset >> 12', 'offset / PAGE_SIZE'],
      'TFU-input-RO' => [4096, 'bo == job->dst ?', 'true ?'],
      'TFU-output-RW' => [4096, 'bo == job->dst ?', 'false ?'],
      'full-BO-tail' => [65536, 'remaining = bo->cma.dmasize;', 'remaining = MIN(bo->cma.dmasize, (size_t)4096);'],
      'all-unique-TFU-BOs' => [4096, 'job->nbo : job->ntfu;', 'job->nbo : MIN(job->ntfu, 2U);'],
      'revoke-selected-only' => [4096, 'V3D_PTE_WRITE : 0) : 0;', 'V3D_PTE_WRITE : 0) : 1;']
    }
    controls.each do |name, (page, before, after)|
      abort 'Nonunique control ' + name unless body.scan(before).size == 1
      File.write(dir + '/ptes-body.h', body.sub(before) { after })
      run.call(name + '-compile', compile.call(page))
      _, err, status = run.call(name, execute, false)
      condition = name == 'revoke-selected-only' ?
        'table[p] == (selected ? 0U : guard)' :
        'table[(bo[b].gpu >> 12) + p] == expected'
      exact = err.lines.any? { |l| l.start_with?('FAIL line ') && l.sub(/^FAIL line \d+: /, '').strip == condition }
      abort 'Noncausal control ' + name unless status.exited? && status.exitstatus == 1 && exact
      puts 'PASS compiled FAIL control ' + name
    end
    File.write(dir + '/ptes-body.h', body)
  end
  run.call('final-compile', compile.call(4096))
  inputs.each { |p, sha| abort 'Input changed: ' + p unless Digest::SHA256.file(p).hexdigest == sha }

ensure
  File.write(dir + '/receipt.json', JSON.pretty_generate({source_sha256: inputs, commands: commands,
    scope: 'Actual native PTE walker. Controlled BO/DMA segments and PT storage, no GPU or 64KiB kernel acceptance.'}) + "\n")
  puts 'Evidence: ' + dir
  FileUtils.remove_entry(dir) unless evidence
end
