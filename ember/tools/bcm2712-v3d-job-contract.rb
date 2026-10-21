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
evidence = ENV['V3D_JOB_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('bcmv3d-job-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(dir)
commands = []; inputs = {}
begin
  %w[bo drv job].each do |name|
    ext = name == 'drv' ? 'h' : 'c'
    path = root + '/sys/external/bsd/drm2/bcmv3d/bcmv3d_' + name + '.' + ext
    text = File.binread(path); inputs[path] = Digest::SHA256.hexdigest(text)
    File.write(dir + '/' + name + '-body.h', text.lines.reject { |l| l.start_with?('#include ', '__KERNEL_RCSID(') }.join)
  end
  %w[ember/tools/bcmv3d-resv-model-export.rb ember/tools/bcm2712-v3d-bo-contract.c
     ember/tools/bcm2712-v3d-sync-model.h ember/tools/bcm2712-v3d-resv-model.h
     ember/tools/bcmv3d-kheapsort-model.h common/lib/libc/stdlib/heapsort.c
     sys/external/bsd/drm2/dist/drm/drm_gem.c
     sys/external/bsd/drm2/bcmv3d/bcmv3d_resv.c sys/external/bsd/drm2/bcmv3d/bcmv3d_resv.h
     sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h
     sys/external/bsd/drm2/bcmv3d/bcmv3d_cl_reject.h
     sys/external/bsd/drm2/bcmv3d/bcmv3d_tfu_reject.h].each do |path|
    inputs[root + '/' + path] = Digest::SHA256.file(root + '/' + path).hexdigest
  end
  command = [ENV.fetch('RUBY', RbConfig.ruby), root + '/ember/tools/bcmv3d-resv-model-export.rb', root, dir]
  out, err, status = capture(command); abort(out + err) unless status.success? && !sanitizer_diagnostic?(out, err)
  path = root + '/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h'
  inputs[path] = Digest::SHA256.file(path).hexdigest
  File.write(dir + '/uapi-body.h', File.readlines(path).reject { |l| l.start_with?('#include ') }.join)
  %w[c rb sh].each { |ext| p = root + '/ember/tools/bcm2712-v3d-job-contract.' + ext; inputs[p] = Digest::SHA256.file(p).hexdigest }
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) +
    %w[-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + dir,
    root + '/ember/tools/bcm2712-v3d-job-contract.c', '-o', dir + '/contract']
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [dir + '/contract']
  run = lambda do |name, argv, required = true|
    out, err, status = capture(argv)
    File.write(dir + '/' + name + '.stdout', out); File.write(dir + '/' + name + '.stderr', err)
    commands << { name: name, argv: argv, exit: status.exitstatus, signal: status.termsig }
    abort name + ': sanitizer diagnostic' if sanitizer_diagnostic?(out, err)
    abort name + ': inspect retained stderr' if required && !status.success?
    [out, err, status]
  end
  process_controls(dir) unless ENV['V3D_SKIP_HARNESS'] == '1'
  run.call('positive-compile', compile)
  if ENV['V3D_COMPILE_ONLY'] == '1'
    FileUtils.cp(dir + '/contract', ENV.fetch('V3D_OUTPUT'))
  else
    out, = run.call('positive', execute); print out
    unless ENV['V3D_SKIP_MUTANTS'] == '1'
      body = File.read(dir + '/job-body.h')
      controls = {
        'flags' => ['bcmv3d_payload', 'a->flags || a->extensions', 'false || a->extensions', 'bcmv3d_submit_tfu(&d,&b,&f)<0 && starts==0 && !c.job->poisoned'],
        'source-write' => ['bcmv3d_ptes', 'bo == job->dst ?', 'true ?', 'flags==(bo==j->dst?(V3D_PTE_VALID|V3D_PTE_WRITE):V3D_PTE_VALID)'],
        'completion-count' => ['bcmv3d_execute', 'atomic_load_acquire(&job->events) != 1', 'false', 'n==11?error==0:error<0'],
        'busy' => ['bcmv3d_execute', '(after & 1) != 0', 'false', 'n==11?error==0:error<0'],
        'unmap' => ['bcmv3d_execute', 'bcmv3d_ptes(job, false)', 'bcmv3d_ptes(job, true)', '!retired && scratch_owned'],
        'revoke' => ['bcmv3d_execute', 'error = bcmv3d_flush();', '(void)bcmv3d_flush(); error = 0;', 'retired && revoke_synced && revoked'],
        'prewrite' => ['bcmv3d_tfu_sync', 'before ? BUS_DMASYNC_PREWRITE : BUS_DMASYNC_POSTWRITE', 'before ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_POSTWRITE', 'ops==(bo==j->dst?3:1) && !tfu_owned[slot]'],
        'preread' => ['bcmv3d_tfu_sync', 'before ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_POSTREAD', 'before ? 0 : BUS_DMASYNC_POSTREAD', 'ops==(bo==j->dst?3:1) && !tfu_owned[slot]'],
        'postread' => ['bcmv3d_tfu_sync', 'before ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_POSTREAD', 'before ? BUS_DMASYNC_PREREAD : 0', 'ops==(bo==j->dst?12:4) && tfu_owned[slot]'],
        'early-return-to-cpu' => ['bcmv3d_execute', 'bcmv3d_ptes(job, false);', 'bcmv3d_tfu_sync(job, false); bcmv3d_ptes(job, false);', 'retired && revoke_synced && revoked'],
        'nested-mask' => ['bcmv3d_mask', 'if (!job->controller_masked)', 'if (true)', '!controller_masked && handler!=NULL'],
        'kernel-IOC' => ['bcmv3d_execute', 'a->icfg | V3D_TFU_IOC', 'a->icfg', '(v & 1U) != 0 && tfu_owned_count == j->ntfu'],
        'unique-reference' => ['bcmv3d_tfu_lookup', 'if (j == *count)', 'if (true)', 'unique==(alias?1U:2U)'],
        'private-storage' => ['bcmv3d_tfu_lookup', 'bo == sc->job->pt || bo == sc->job->scratch', 'false', 'error==EINVAL && count==role'],
        'poison-retains-prior-vector' => ['bcmv3d_submit_tfu', "if (job->poisoned) {\n\t\terror = EIO;", "if (job->poisoned) {\n\t\tjob->tfu_bos[0] = NULL;\n\t\terror = EIO;", 'c.job->ntfu==4 && memcmp(retained,c.job->tfu_bos,sizeof(retained))==0 && reg_calls==before'],
        'success-clears-vector' => ['bcmv3d_submit_tfu', 'job->tfu_bos[i] = NULL;', '(void)i;', 'c.job->tfu_bos[i]==NULL']
      }
      controls.each do |name, (part, before, after, condition)|
        fragment = function(body, part)[:text]
        abort 'Nonunique control ' + name unless fragment.scan(before).size == 1
        File.write(dir + '/job-body.h', body.sub(fragment) { fragment.sub(before) { after } })
        run.call(name + '-compile', compile)
        _, err, status = run.call(name, execute, false)
        exact = err.lines.any? { |l| l.sub(/^FAIL line \d+: /, '').strip == condition && l.start_with?('FAIL line ') }
        abort 'Noncausal control ' + name unless status.exited? && status.exitstatus == 1 && exact
        puts 'PASS compiled FAIL control ' + name
        File.write(dir + '/job-body.h', body)
      end
    end
    run.call('final-compile', compile)
  end
  inputs.each { |p, sha| abort 'Input changed: ' + p unless Digest::SHA256.file(p).hexdigest == sha }
ensure
  File.write(dir + '/receipt.json', JSON.pretty_generate({source_sha256: inputs, commands: commands,
    scope: 'Actual native job/BO/reservation bodies. Controlled IRQ, DMA, DRM and execution services; no GPU or rendered-pixel acceptance.'}) + "\n")
  puts 'Evidence: ' + dir if evidence
  FileUtils.remove_entry(dir) unless evidence
end
