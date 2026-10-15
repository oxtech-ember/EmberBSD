#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted replay of configured libc compiler commands.
require 'fileutils'
require 'open3'
require 'shellwords'
require 'json'
require 'digest'
abort 'Usage: libc-guard-target.rb CONFIGURED_NBMAKE CANDIDATE_SOURCE NEW_OUTPUT' unless ARGV.size == 3
make, src = ARGV.first(2).map { |p| File.realpath(p) }
out = File.expand_path(ARGV[2])
abort 'Output must not exist' if File.exist?(out)
FileUtils.mkdir_p(out)
def checked(*args, **opts)
  stdout, stderr, status = Open3.capture3(*args, **opts)
  abort "Command failed: #{args.first}: #{stdout}#{stderr}" unless status.success?
  stdout
end
original = checked(make, '-V', 'NETBSDSRCDIR').strip
libc = original + '/lib/libc'
obj = checked(make, '-C', libc, '-V', '.OBJDIR').strip
common = '${COMPILE.c} ${COPTS.stack_protector.c} ${CPUFLAGS.stack_protector.c} ${CPPFLAGS.stack_protector.c}'
variants = {'o' => '', 'pico' => ' ${CSHLIBFLAGS} ${CSHLIBFLAGS.stack_protector.c}',
  'po' => ' ${PROFFLAGS} ${PGFLAGS}'}
file = '/lib/libc/misc/stack_protector.c'
baseline = File.read(original + file)
candidate = File.read(src + file)
abort 'Require distinct baseline and candidate' if baseline == candidate
cc = nil
receipt = {original: original, candidate: src, object_context: obj, variants: []}
{'baseline' => baseline, 'candidate' => candidate}.each do |label, text|
  source = out + '/' + label + '.c'
  File.write(source, text)
  variants.each do |extension, extra|
    command = checked(make, '-C', libc, '-V', common + extra).strip
    args = Shellwords.split(command)
    cc ||= args.first
    abort 'Require actual strong SSP and Werror' unless args.include?('-fstack-protector-strong') && args.include?('-Werror')
    output = out + '/' + label + '.' + extension
    checked(*args, source, '-o', output, chdir: obj)
    dis = checked(File.dirname(cc) + '/x86_64--netbsd-objdump', '-dr', output)
    File.write(output + '.dis', dis)
    guard = dis[/^[0-9a-f]+ <__guard_setup>:\n(.*?)(?=\n[0-9a-f]+ <|\z)/m, 1]
    abort 'Missing actual __guard_setup disassembly' unless guard
    protected_guard = guard.include?('__stack_chk_fail')
    abort "Wrong guard instrumentation: #{label}.#{extension}" unless protected_guard == (label == 'baseline')
    # The failure routine still has a protected frame; only setup is exempt.
    rest = dis.sub(guard, '')
    abort 'Other SSP frames disappeared' unless rest.include?('__stack_chk_guard')
    receipt[:variants] << {label: label, variant: extension, argv: args + [source, '-o', output],
      sha256: Digest::SHA256.file(output).hexdigest, setup_protected: protected_guard}
    next unless extension == 'o'
    stdout, stderr, status = Open3.capture3(*args, '-fanalyzer', source, '-o', output + '.analyzer', chdir: obj)
    File.write(out + '/' + label + '.analyzer.log', stdout + stderr)
    abort 'GCC analyzer failed' unless status.success? && stderr.empty?
    pp = args.reject { |arg| arg == '-c' }
    checked(*pp, '-E', source, '-o', out + '/' + label + '.i', chdir: obj)
    if ENV['CPPCHECK']
      stdout, stderr, status = Open3.capture3(ENV['CPPCHECK'], '--enable=warning,performance,portability',
        '--error-exitcode=1', '--platform=unix64', '--std=c11', out + '/' + label + '.i')
      File.write(out + '/' + label + '.cppcheck.log', stdout + stderr)
      abort 'Cppcheck failed' unless status.success?
      stdout, stderr, status = Open3.capture3(ENV['CPPCHECK'], '--enable=style',
        '--platform=unix64', '--std=c11', out + '/' + label + '.i')
      File.write(out + '/' + label + '.cppcheck-style.log', stdout + stderr)
      abort 'Cppcheck style invocation failed' unless status.success?
    end
  end
end
receipt[:compiler] = checked(cc, '--version').lines.first.strip
receipt[:triplet] = checked(cc, '-dumpmachine').strip
abort 'Require genuine GCC16 amd64' unless receipt[:compiler].include?('16.2.0') && receipt[:triplet] == 'x86_64--netbsd'
receipt[:source_sha256] = Digest::SHA256.hexdigest(candidate)
File.write(out + '/receipt.json', JSON.pretty_generate(receipt) + "\n")
puts 'PASS: actual configured libc .o/.pico/.po, function-only SSP exception and baseline/candidate analysis'
