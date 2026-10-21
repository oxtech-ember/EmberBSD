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
  policy = read.call(native + 'bcmv3d_resv.h')
  sort = read.call('common/lib/libc/stdlib/heapsort.c')
  read.call('ember/tools/bcmv3d-kheapsort-model.h')
  File.write(dir + '/heapsort-body.h', sort.lines.reject { |line| line.start_with?('#include ') }.join)
  uapi = read.call('sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h')
  %w[c rb].each { |ext| read.call('ember/tools/bcmv3d-cl-reject-contract.' + ext) }
  types = %w[V3D_CSD_BO_MAX V3D_MSK_SET V3D_IRQ_ALL V3D_CORE_IRQ_ALL V3D_CSD_STATUS V3D_COPY_SIZE].map do |name|
    job[/^#define #{name}\s+[^\n]+/] || abort('Missing native constant ' + name)
  end.join("\n") + "\n" + policy[/^#define BCMV3D_RESV_MAX\s+[^\n]+/] + "\n" + header + "\n" +
    uapi[/^#define DRM_V3D_SUBMIT_CL_FLUSH_CACHE\s+[^\n]+/] + "\n" +
    definition(uapi, 'submit-cl-uapi', /^struct drm_v3d_submit_cl \{/)[:text] +
    definition(uapi, 'submit-csd-uapi', /^struct drm_v3d_submit_csd \{/)[:text]
  structs = %w[bcmv3d_job bcmv3d_sync].map do |name|
    fragment = definition(job, name, /^struct #{name} \{/)
    fragments << fragment.reject { |key, _| key == :text }; fragment[:text]
  end.join("\n")
  names = %w[bcmv3d_ptes bcmv3d_sync_prepare bcmv3d_csd_address bcmv3d_object_order bcmv3d_job_lookup bcmv3d_cl_reject_take bcmv3d_cl_reject bcmv3d_submit_cl bcmv3d_submit_csd]
  parts = {}; body = names.map do |name|
    fragment = function(job, name)
    fragments << fragment.reject { |key, _| key == :text }; parts[name] = fragment[:text]
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
      'saturating-ceiling' => ['bcmv3d_cl_reject_take', 'before >= BCMV3D_CL_REJECT_MAX', 'before > BCMV3D_CL_REJECT_MAX'],
      'counter-per-device' => ['bcmv3d_cl_reject_take', 'before = atomic_load_relaxed(&job->cl_rejections);', '(void)job; static unsigned int shared; before = atomic_load_relaxed(&shared);'],
      'CAS-ticket-ownership' => ['bcmv3d_cl_reject_take', 'before + 1) ==', 'before + 1) !='],
      'saved-count' => ['bcmv3d_cl_reject', 'error, a->bo_handle_count, a->flags', 'error, 64U, a->flags'],
      'extension-presence' => ['bcmv3d_cl_reject', '(unsigned int)(a->extensions != 0)', '(unsigned int)a->extensions'],
      'only-EINVAL' => ['bcmv3d_cl_reject', 'error != EINVAL ||', 'false ||'],
      'sync-rcl-reason' => ['bcmv3d_submit_cl', 'reason = BCMV3D_CL_REJECT_SYNC_RCL;', 'reason = BCMV3D_CL_REJECT_SYNC_BCL;'],
      'wait-rcl-reason' => ['bcmv3d_submit_cl', 'reason = BCMV3D_CL_REJECT_WAIT_RCL;', 'reason = BCMV3D_CL_REJECT_WAIT_BCL;'],
      'post-unwind-only' => ['bcmv3d_submit_cl', "release:\n\tbcmv3d_resv_unlock", "release:\n\tbcmv3d_cl_reject(sc, a, reason, error);\n\tbcmv3d_resv_unlock"],
      'execution-not-admission' => ['bcmv3d_submit_cl', "reason = BCMV3D_CL_REJECT_NONE;\n\t/* Completion", "reason = BCMV3D_CL_REJECT_RESV_PUBLISH;\n\t/* Completion"],
      'original-errno' => ['bcmv3d_submit_cl', "\treturn -error;\n}", "\treturn error;\n}"],
      'reference-release' => ['bcmv3d_submit_cl', 'drm_gem_object_put_unlocked(objects[--count]);', '((void)drm_gem_object_put_unlocked, (void)objects[--count]);'],
      'old-CL-limit64' => ['bcmv3d_submit_cl', 'a->bo_handle_count > V3D_CSD_BO_MAX', 'a->bo_handle_count > 64'],
      'old-CSD-limit64' => ['bcmv3d_submit_csd', 'a->bo_handle_count > V3D_CSD_BO_MAX', 'a->bo_handle_count > 64'],
      'maximum-inclusive' => ['bcmv3d_job_lookup', 'capacity > BCMV3D_RESV_MAX', 'capacity >= BCMV3D_RESV_MAX'],
      'nonblocking-metadata' => ['bcmv3d_job_lookup', 'kmem_zalloc(object_bytes, KM_NOSLEEP)', 'kmem_zalloc(object_bytes, KM_SLEEP)'],
      'no-GEM-sort' => ['bcmv3d_job_lookup', "kheapsort(objects, count, sizeof(*objects), bcmv3d_object_order,\n\t    &temporary) != 0", '((void)&temporary, (void)bcmv3d_object_order, (void)kheapsort, false)'],
      'partial-capacity-free' => ['bcmv3d_job_lookup', 'kmem_free(objects, object_bytes);', 'kmem_free(objects, count * sizeof(*objects));'],
      'release-partial-refs' => ['bcmv3d_job_lookup', 'drm_gem_object_put_unlocked(objects[--count]);', '((void)drm_gem_object_put_unlocked, (void)objects[--count]);'],
      'success-vector-clear' => ['bcmv3d_submit_cl', 'job->bos = NULL;', '(void)job;'],
      'poison-keeps-vector' => ['bcmv3d_submit_cl', 'objects = NULL; /* Transfer the vector with its retained refs. */', '/* Lost retained-vector ownership. */'],
      'poison-keeps-CSD-vector' => ['bcmv3d_submit_csd', 'objects = NULL; /* Transfer the vector with its retained refs. */', '/* Lost retained-vector ownership. */'],
      'second-poison-no-assignment' => ['bcmv3d_submit_cl', 'if (job->poisoned) {', "job->bos = objects;\n\tif (job->poisoned) {"],
      'second-CSD-poison-no-assignment' => ['bcmv3d_submit_csd', 'if (job->poisoned) {', "job->bos = objects;\n\tif (job->poisoned) {"],
      'release-after-last-unlock' => ['bcmv3d_submit_cl', "release:\n\tbcmv3d_resv_unlock(&reservations);", "release:\n\t/* Missing final reservation release. */"],
      'full-PTE-vector' => ['bcmv3d_ptes', 'i < count', 'i < MIN(count, 64U)']
    }
    controls.each do |name, (part, before, after)|
      text = parts.fetch(part)
      abort 'Nonunique control ' + name unless text.scan(before).size == 1
      changed = text.sub(before) { after }
      mutated = body.sub(text) { changed }
      if name == 'counter-per-device'
        mutated = mutated.sub('atomic_cas_uint(&job->cl_rejections, before, before + 1)',
          'atomic_cas_uint(&shared, before, before + 1)')
      end
      File.write(dir + '/reject-body.h', mutated)
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
      'Actual CL/CSD lookup, range/PTE helper, UAPI, private job/sync structs, counter and receipt; native fixed-workspace sort.'
    ] }) + "\n")
  puts 'Evidence: ' + dir
  FileUtils.remove_entry(dir) unless evidence
end
