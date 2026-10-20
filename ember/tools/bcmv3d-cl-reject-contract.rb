#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
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
evidence = ENV['CL_REJECT_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('v3d-cl-reject-', ENV.fetch('TMPDIR', '/tmp'))
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
  header = read.call(native + 'bcmv3d_cl_reject.h')
  uapi = read.call('sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h')
  %w[c rb].each { |ext| read.call('ember/tools/bcmv3d-cl-reject-contract.' + ext) }
  types = %w[V3D_CSD_BO_MAX V3D_MSK_SET V3D_IRQ_ALL V3D_CORE_IRQ_ALL].map do |name|
    job[/^#define #{name}\s+[^\n]+/] || abort('Missing native constant ' + name)
  end.join("\n") + "\n" + header + "\n" +
    uapi[/^#define DRM_V3D_SUBMIT_CL_FLUSH_CACHE\s+[^\n]+/] + "\n" +
    definition(uapi, 'submit-cl-uapi', /^struct drm_v3d_submit_cl \{/)[:text]
  structs = %w[bcmv3d_job bcmv3d_sync].map do |name|
    fragment = definition(job, name, /^struct #{name} \{/)
    fragments << fragment.reject { |key, _| key == :text }; fragment[:text]
  end.join("\n")
  names = %w[bcmv3d_csd_address bcmv3d_cl_reject_take bcmv3d_cl_reject bcmv3d_submit_cl]
  body = names.map do |name|
    fragment = function(job, name)
    fragments << fragment.reject { |key, _| key == :text }; fragment[:text]
  end.join("\n")
  File.write(dir + '/reject-types.h', types)
  File.write(dir + '/reject-structs.h', structs)
  File.write(dir + '/reject-body.h', body)
  File.write(dir + '/source-job.c', job)
  run = lambda do |name, argv, required = true|
    out = ''; err = ''; status = nil; timeout = false
    Open3.popen3(*argv, pgroup: true) do |stdin, stdout, stderr, waiter|
      stdin.close
      readers = [Thread.new { out = stdout.read }, Thread.new { err = stderr.read }]
      unless waiter.join(120)
        timeout = true
        begin; Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
      end
      status = waiter.value; readers.each(&:join)
    end
    File.write(dir + '/' + name + '.stdout', out)
    File.write(dir + '/' + name + '.stderr', err)
    commands << { name: name, argv: argv, exit: status.exitstatus, signal: status.termsig, timeout: timeout }
    abort name + ': inspect retained stderr' if required && (timeout || !status.success?)
    [out, err, status, timeout]
  end
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) +
    %w[-std=c11 -Wall -Wextra -Werror -pthread] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + dir,
    root + '/ember/tools/bcmv3d-cl-reject-contract.c', '-o', dir + '/contract']
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', ''))
  run.call('positive-compile', compile)
  out, = run.call('positive', execute + [compile[-1]]); print out
  unless ENV['CL_REJECT_SKIP_MUTANTS'] == '1'
    controls = {
      'saturating-ceiling' => ['before >= BCMV3D_CL_REJECT_MAX', 'before > BCMV3D_CL_REJECT_MAX'],
      'counter-per-device' => ['before = atomic_load_relaxed(&job->cl_rejections);',
        "(void)job; static unsigned int shared; before = atomic_load_relaxed(&shared);"],
      'CAS-ticket-ownership' => ['before + 1) ==', 'before + 1) !='],
      'saved-count' => ['error, a->bo_handle_count, a->flags', 'error, 64U, a->flags'],
      'extension-presence' => ['(unsigned int)(a->extensions != 0)', '(unsigned int)a->extensions'],
      'only-EINVAL' => ['error != EINVAL ||', 'false ||'],
      'sync-rcl-reason' => ['reason = BCMV3D_CL_REJECT_SYNC_RCL;', 'reason = BCMV3D_CL_REJECT_SYNC_BCL;'],
      'wait-rcl-reason' => ['reason = BCMV3D_CL_REJECT_WAIT_RCL;', 'reason = BCMV3D_CL_REJECT_WAIT_BCL;'],
      'post-unwind-only' => ["release:\n\tbcmv3d_resv_unlock", "release:\n\tbcmv3d_cl_reject(sc, a, reason, error);\n\tbcmv3d_resv_unlock"],
      'execution-not-admission' => ["reason = BCMV3D_CL_REJECT_NONE;\n\t/* Completion", "reason = BCMV3D_CL_REJECT_RESV_PUBLISH;\n\t/* Completion"],
      'original-errno' => ["\treturn -error;\n}", "\treturn error;\n}"],
      'reference-release' => ['drm_gem_object_put_unlocked(objects[--count]);', '((void)drm_gem_object_put_unlocked, (void)objects[--count]);']
    }
    controls.each do |name, (before, after)|
      abort 'Nonunique control ' + name unless body.scan(before).size == 1
      changed = body.sub(before) { after }
      if name == 'counter-per-device'
        changed = changed.sub('atomic_cas_uint(&job->cl_rejections, before, before + 1)',
          'atomic_cas_uint(&shared, before, before + 1)')
      end
      File.write(dir + '/reject-body.h', changed)
      run.call(name + '-compile', compile)
      _, err, status, timeout = run.call(name, execute + [compile[-1]], false)
      abort 'Noncausal control ' + name unless !timeout && status.exited? && status.exitstatus == 1 && err.include?('FAIL line ')
      puts 'PASS compiled FAIL control ' + name
      File.write(dir + '/reject-body.h', body)
    end
  end
  # Restore and recompile the checked positive body for retained analysis.
  run.call('final-compile', compile)
  inputs.each do |path, hash|
    abort 'Input changed: ' + path unless Digest::SHA256.file(root + '/' + path).hexdigest == hash
  end
ensure
  File.write(dir + '/receipt.json', JSON.pretty_generate({ inputs_sha256: inputs,
    fragments: fragments, commands: commands, limitations: [
      'Controlled kernel/DRM boundaries; no GPU, MMIO, hardware or scheduler acceptance.',
      'Injected helper EINVAL validates routing, not native helper reachability.',
      'Actual range helper, UAPI, private job/sync structs, counter, receipt and submit body.'
    ] }) + "\n")
  puts 'Evidence: ' + dir
  FileUtils.remove_entry(dir) unless evidence
end
