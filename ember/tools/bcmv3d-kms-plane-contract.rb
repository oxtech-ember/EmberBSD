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
  name_pos = text.index(/^#{Regexp.escape(name)}\(/)
  abort 'Missing function: ' + name unless name_pos
  first = text.rindex(/^static\b/, name_pos)
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

input = File.expand_path(ARGV[0] || File.expand_path('../..', __dir__))
source = File.directory?(input) ? input + '/sys/external/bsd/drm2/bcmv3d/bcmv3d_kms.c' : input
abort 'Pass a repository root or actual KMS C source file' unless File.file?(source)
source = File.realpath(source)
evidence = ENV['KMS_PLANE_EVIDENCE']
abort 'New evidence directory required' if evidence && File.exist?(evidence)
dir = evidence || Dir.mktmpdir('v3d-kms-plane-', ENV.fetch('TMPDIR', '/tmp'))
FileUtils.mkdir_p(dir)
inputs = {}; commands = []; fragments = []
begin
  original = File.binread(source)
  inputs[source] = Digest::SHA256.hexdigest(original)
  File.write(dir + '/source-kms.c', original)
  header = source.sub(/\.c\z/, '.h')
  if File.file?(header)
    inputs[header] = Digest::SHA256.file(header).hexdigest
    FileUtils.cp(header, dir + '/source-kms.h')
  end
  helper = File.expand_path('../dist/drm/drm_atomic_state_helper.c', File.dirname(source))
  inputs[helper] = Digest::SHA256.file(helper).hexdigest if File.file?(helper)
  %w[c rb].each do |extension|
    path = __dir__ + '/bcmv3d-kms-plane-contract.' + extension
    inputs[path] = Digest::SHA256.file(path).hexdigest
  end
  fragments << definition(original, 'plane-state', /^struct bcmv3d_kms_plane_state \{/)
  fragments << definition(original, 'fixed-mode', /^static const struct drm_display_mode bcmv3d_kms_mode = \{/)
  fragments << function(original, 'kms_plane_state')
  fragments << function(original, 'kms_mode_valid')
  fragments << definition(original, 'framebuffer-funcs', /^static const struct drm_framebuffer_funcs kms_fb_funcs = \{/)
  %w[kms_fb_create kms_plane_cleanup kms_plane_destroy_state kms_plane_reset
     kms_plane_duplicate kms_plane_prepare kms_plane_check].each do |name|
    fragments << function(original, name)
  end
  write_body = lambda do |parts|
    File.write(dir + '/kms-plane-body.h', parts.map { |part|
      '#line ' + part[:line].to_s + ' ' + source.dump + "\n" + part[:text]
    }.join("\n"))
  end
  write_body.call(fragments)
  File.write(dir + '/fragments.json', JSON.pretty_generate(fragments.map { |part|
    { name: part[:name], first_line: part[:line], sha256: Digest::SHA256.hexdigest(part[:text]) }
  }) + "\n")
  run = lambda do |name, argv, required = true|
    out = ''; err = ''; status = nil; timeout = false
    Open3.popen3(*argv, pgroup: true) do |stdin, stdout, stderr, waiter|
      stdin.close
      readers = [Thread.new { out = stdout.read }, Thread.new { err = stderr.read }]
      unless waiter.join(name.end_with?('compile') ? 180 : 30)
        timeout = true
        begin; Process.kill('KILL', -waiter.pid); rescue Errno::ESRCH; end
      end
      status = waiter.value; readers.each(&:join)
    end
    File.write(dir + '/' + name + '.stdout', out); File.write(dir + '/' + name + '.stderr', err)
    commands << { name: name, argv: argv, exit: status.exitstatus, signal: status.termsig, timeout: timeout }
    abort "#{name}: #{err}" if required && (timeout || !status.success?)
    [out, err, status, timeout]
  end
  compile = Shellwords.split(ENV.fetch('CC', 'cc')) + %w[-std=c11 -Wall -Wextra -Werror] +
    Shellwords.split(ENV.fetch('CFLAGS', '')) + ['-I' + dir,
    __dir__ + '/bcmv3d-kms-plane-contract.c', '-o', dir + '/contract']
  execute = Shellwords.split(ENV.fetch('TEST_RUNNER', '')) + [dir + '/contract']
  run.call('compile', compile)
  out, = run.call('positive', execute); print out
  unless ENV['KMS_PLANE_SKIP_MUTANTS'] == '1'
    controls = {
      'mode-timing' => ['kms_mode_valid', 'm->hsync_start == fixed->hsync_start', 'true'],
      'fb-format' => ['kms_fb_create', 'cmd->pixel_format != DRM_FORMAT_XRGB8888 ||', 'false ||'],
      'unused-plane' => ['kms_fb_create', 'for (i = 1; i < 4; i++)', 'for (i = 4; i < 4; i++)'],
      'native-identity' => ['kms_fb_create', 'bcmv3d_bo_reference(dev->dev_private,', 'bcmv3d_bo_reference(NULL,'],
      'fb-reference-put' => ['kms_fb_create', 'drm_gem_object_put_unlocked(&bo->cma.base);', '(void)bo;'],
      'fb-failure-unwind' => ['kms_fb_create', 'drm_framebuffer_put(fb);', '(void)fb;'],
      'duplicate-private' => ['kms_plane_duplicate', '__drm_atomic_helper_plane_duplicate_state(plane, &s->base);',
        '__drm_atomic_helper_plane_duplicate_state(plane, &s->base); s->frame = kms_plane_state(plane->state)->frame;'],
      'reserve-only' => ['kms_plane_prepare', 'bcmv3d_scanout_reserve(', '((void)bcmv3d_scanout_reserve, bcmv3d_scanout_prepare)('],
      'explicit-fence-get' => ['kms_plane_prepare', 'dma_fence_get(state->fence)', 'state->fence'],
      'inactive-no-reserve' => ['kms_plane_prepare', 'crtc == NULL || !crtc->active', 'crtc == NULL'],
      'prepare-failure-fence' => ['kms_plane_prepare', 'dma_fence_put(s->explicit);', '(void)s;'],
      'cleanup-consumes-once' => ['kms_plane_cleanup', 's->frame = NULL;', '(void)s;'],
      'cleanup-explicit-once' => ['kms_plane_cleanup', 's->explicit = NULL;', '(void)s;'],
      'destroy-base-state' => ['kms_plane_destroy_state', '__drm_atomic_helper_plane_destroy_state(state);', '(void)__drm_atomic_helper_plane_destroy_state;'],
      'reset-old-state' => ['kms_plane_reset', 'kms_plane_destroy_state(plane, plane->state);', '(void)plane;'],
      'no-scaling' => ['kms_plane_check', 'DRM_PLANE_HELPER_NO_SCALING, DRM_PLANE_HELPER_NO_SCALING',
        '0, DRM_PLANE_HELPER_NO_SCALING'],
      'full-source-geometry' => ['kms_plane_check', 'state->src_x != 0 ||', 'false ||'],
      'active-invisible-original' => ['kms_plane_check', "if (!state->visible)\n\t\treturn -EINVAL;", "if (!state->visible)\n\t\treturn 0;"],
      'active-invisible' => ['kms_plane_check', 'if (!state->visible)', 'if (false)']
    }
    controls.each do |name, (function_name, before, after)|
      part = fragments.find { |item| item[:name] == function_name }
      abort 'Unique function control required: ' + name unless part[:text].scan(before).size == 1
      changed = fragments.map { |item| item.equal?(part) ? item.merge(text: item[:text].sub(before) { after }) : item }
      write_body.call(changed); run.call(name + '-compile', compile)
      _, err, status, timeout = run.call(name, execute, false)
      abort 'Noncausal function control: ' + name unless !timeout && status.exited? &&
        status.exitstatus == 1 && err.lines.any? { |line| line.start_with?('CHECK ') }
      puts 'PASS compiled CHECK control ' + name
    end
    write_body.call(fragments)
  end
  inputs.each do |path, expected|
    abort 'Input changed during checks; rerun on its fresh hash: ' + path unless Digest::SHA256.file(path).hexdigest == expected
  end
  puts 'PASS fresh KMS input SHA256 ' + inputs[source]
ensure
  File.write(dir + '/inputs.json', JSON.pretty_generate(inputs) + "\n")
  File.write(dir + '/commands.json', JSON.pretty_generate(commands) + "\n")
  FileUtils.remove_entry(dir) unless evidence
end
