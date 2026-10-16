# SPDX-License-Identifier: BSD-2-Clause
# Build only. Never execute the target program or mutate the sysroot.
require 'open3'
require 'json'
require 'digest'
require 'fileutils'
root, cc, sysroot, runtime, provenance, work = ARGV
abort 'Usage: ruby usl-vt-vm-build.rb SOURCE GCC SYSROOT RUNTIME OS_PROVENANCE RECEIPT' unless work && ARGV.length == 6
root, cc, sysroot, runtime, provenance, work = ARGV.map { |p| File.expand_path(p) }
abort 'Use a new private receipt directory' if File.exist?(work)
FileUtils.mkdir_p(work)
run = lambda do |name, argv|
  out, err, status = Open3.capture3(*argv)
  File.write(work + '/' + name + '.log', out + err)
  abort "#{name} failed: see receipt" unless status.success?
  out
end
abort 'Build the final runner from a clean committed source tree' unless run.call('source-status', ['git', '-C', root, 'status', '--porcelain=v1']).empty?
triplet = run.call('target', [cc, '-dumpmachine']).strip
abort 'Require the amd64 EmberBSD application GCC 16.2.0' unless triplet == 'x86_64--netbsd' && run.call('version', [cc, '-dumpfullversion']).strip == '16.2.0'
readelf = File.dirname(cc) + '/' + triplet + '-readelf'
abort 'Missing OS provenance receipt' unless File.file?(provenance) && File.size(provenance) > 0
source = root + '/ember/tests/usl-vt-vm.c'
crt = runtime + '/lib/gcc/' + triplet + '/16.2.0'
headers = %w[wsconsio.h wsdisplay_usl_io.h].map do |name|
  original = root + '/sys/dev/wscons/' + name
  staged = sysroot + '/usr/include/dev/wscons/' + name
  abort "Header mismatch: #{name}" unless Digest::SHA256.file(original).hexdigest == Digest::SHA256.file(staged).hexdigest
  staged
end
flags = ['--sysroot=' + sysroot, '-nostdinc', '-isystem', sysroot + '/usr/include', '-B' + crt + '/', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror']
run.call('header-probe', [cc, *flags, '-H', '-fsyntax-only', source])
used_headers = File.read(work + '/header-probe.log').lines.filter_map { |l| l[/^\.+ (\/.*)$/, 1] }.uniq
abort 'Unexpected header provider' if used_headers.empty? || used_headers.any? { |p| !p.start_with?(sysroot + '/usr/include/') }
inputs = [source, __FILE__, root + '/sys/dev/wscons/wsdisplay_compat_usl.c', cc, readelf, provenance, *headers, *used_headers, *%w[crt0.o crti.o crtn.o libc.a].map { |n| sysroot + '/usr/lib/' + n }, *%w[crtbeginT.o crtend.o libgcc.a libgcc_eh.a].map { |n| crt + '/' + n }]
hashes = lambda { inputs.to_h { |p| [p, Digest::SHA256.file(p).hexdigest] } }
before = hashes.call
File.write(work + '/inputs-before.json', JSON.pretty_generate(before) + "\n")
# The C runner has no C++ dependency. Keep the OS CRT and libc in this sysroot.
argv = [cc, *flags, '-static', '-H', source, '-Wl,-Map,' + work + '/link.map', '-o', work + '/usl-vt-vm']
File.write(work + '/argv.json', JSON.pretty_generate(argv) + "\n")
run.call('compile', argv)
after = hashes.call
abort 'Inputs changed during the build; output is unqualified' unless before == after
elf = run.call('elf', [readelf, '-h', '-l', '-d', '-n', work + '/usl-vt-vm'])
abort 'Require a static amd64 NetBSD ABI ELF' unless elf.include?('Advanced Micro Devices X86-64') && elf.include?('ELF64') && elf.include?('NetBSD') && !elf.match?(/\b(?:INTERP|NEEDED|RPATH|RUNPATH)\b/)
map = File.read(work + '/link.map')
%w[crt0.o crti.o crtn.o libc.a].each { |name| abort "Wrong OS provider: #{name}" unless map.include?(sysroot + '/usr/lib/' + name) }
%w[crtbeginT.o crtend.o libgcc.a].each { |name| abort "Wrong GCC provider: #{name}" unless map.include?(crt + '/' + name) }
File.write(work + '/receipt.json', JSON.pretty_generate({scope: 'cross-built only; no VM/device execution', source_commit: run.call('source-commit', ['git', '-C', root, 'rev-parse', 'HEAD']).strip, inputs: after, elf_sha256: Digest::SHA256.file(work + '/usl-vt-vm').hexdigest, acceptance: 'pending; rebuild from a frozen qualified sysroot before VM acceptance'}) + "\n")
puts 'PASS cross-built static target ELF and explicit OS/GCC providers; runtime acceptance pending'
