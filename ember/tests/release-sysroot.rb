# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted), 2026-10-16.
# Exercise the actual builder and sysroot release contract with host fixtures.
# The ELF headers below are structural unit data, not executable target evidence.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'rubygems/package'
require 'stringio'
require 'tmpdir'
require_relative '../release/sysroot-kit'

def run!(*command)
  output, error, result = Open3.capture3(*command)
  abort "FAIL: #{command.inspect}: #{output}#{error}" unless result.success?
  output
end

def refuse(name, expected)
  yield
rescue EmberRelease::Error, SystemCallError => error
  abort "FAIL: #{name} stopped for another reason: #{error.message}" unless expected.match?(error.message)
  puts "PASS: refuses #{name}"
else
  abort "FAIL: accepted #{name}"
end

def write_json(path, object)
  File.write(path, JSON.pretty_generate(object) + "\n")
end

def reseal_kit(dir)
  manifest_path = File.join(dir, 'manifest.json')
  manifest = JSON.parse(File.read(manifest_path))
  yield manifest if block_given?
  manifest.fetch('files').each do |file|
    path = File.join(dir, file.fetch('name'))
    file['size'] = File.size(path)
    file['sha256'] = Digest::SHA256.file(path).hexdigest
  end
  write_json(manifest_path, manifest)
  File.write(File.join(dir, 'SHA256'), EmberRelease.checksum_text(dir,
    manifest.fetch('files').map { |file| file.fetch('name') } + ['manifest.json']))
end

def rewrite_archive(path, work)
  Dir.mkdir(work)
  run!('tar', '-xJf', path, '-C', work)
  yield work
  run!('tar', '-cJf', path, '-C', work, '.')
end

builder = File.expand_path('../release/build-aarch64.sh', __dir__)
cli = File.expand_path('../release/sysroot.rb', __dir__)
Dir.mktmpdir('ember-release-sysroot') do |dir|
  repo = File.join(dir, 'fixture-source')
  FileUtils.mkdir_p(File.join(repo, 'ember/release'))
  FileUtils.cp(builder, File.join(repo, 'ember/release/build-aarch64.sh'))
  %w[tools/Makefile share/mk/bsd.own.mk sys/arch/evbarm/conf/EMBER64].each do |name|
    path = File.join(repo, name)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, "# Source completeness sentinel for a host fixture only.\n")
  end
  # ELFCLASS64, ELFDATA2LSB, EV_CURRENT, and EM_AARCH64. No executable body.
  elf = "\x7fELF\x02\x01\x01".b.ljust(64, "\0")
  elf[18, 2] = [183].pack('v')
  base = File.join(repo, 'fixture-base')
  comp = File.join(repo, 'fixture-comp')
  {'libexec/ld.elf_so' => elf, 'lib/libc.so.12.229' => elf}.each do |name, bytes|
    path = File.join(base, name)
    FileUtils.mkdir_p(File.dirname(path))
    File.binwrite(path, bytes)
  end
  File.symlink('libc.so.12.229', File.join(base, 'lib/libc.so.12'))
  %w[stdio.h stdlib.h pthread.h].each do |name|
    path = File.join(comp, 'usr/include', name)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, "/* Structural fixture header. */\n")
  end
  %w[crt0.o crti.o crtn.o libc.a libm.a libpthread.a].each do |name|
    path = File.join(comp, 'usr/lib', name)
    FileUtils.mkdir_p(File.dirname(path))
    File.binwrite(path, name.end_with?('.o') ? elf : "!<arch>\n")
  end
  File.binwrite(File.join(repo, 'fixture-kernel'), elf)
  File.write(File.join(repo, 'build.sh'), <<~'SH')
    #!/bin/sh
    # Host orchestration fixture. This does not build an operating system.
    set -eu
    previous=
    for argument do
      case "$previous" in -T) tools=$argument ;; -O) obj=$argument ;; -R) release=$argument ;; esac
      case "$argument" in BUILDINFO=*) buildinfo=${argument#BUILDINFO=} ;; esac
      previous=$argument
    done
    out=$(dirname "$tools")
    printf '%s\n' "$@" >> "$out/observed-arguments"
    if test "$previous" = kernel=EMBER64; then
      mkdir -p "$tools/bin" "$obj/kernels/EMBER64"
      printf '#!/bin/sh\nprintf "fixture bootstrap GCC 12.5.0\\n"\n' > "$tools/bin/aarch64--netbsd-gcc"
      chmod +x "$tools/bin/aarch64--netbsd-gcc"
      cp fixture-kernel "$obj/kernels/EMBER64/netbsd"
      printf '%s\n\000' "$buildinfo" >> "$obj/kernels/EMBER64/netbsd"
    elif test "$previous" = sets; then
      test -s "$obj/kernels/EMBER64/netbsd"
      mkdir -p "$release/evbarm-aarch64/binary/sets"
      tar -cJf "$release/evbarm-aarch64/binary/sets/base.tar.xz" -C fixture-base .
      tar -cJf "$release/evbarm-aarch64/binary/sets/comp.tar.xz" -C fixture-comp .
    else
      exit 70
    fi
  SH
  run!('git', 'init', '-q', repo)
  run!('git', '-C', repo, 'add', '.')
  run!('git', '-C', repo, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
    'commit', '-qm', 'Structural sysroot contract fixture, not target acceptance')
  pin = run!('git', '-C', repo, 'rev-parse', 'HEAD').strip
  build = File.join(dir, 'build')
  run!('sh', File.join(repo, 'ember/release/build-aarch64.sh'), pin, build)
  profile = JSON.parse(File.read(File.join(build, 'build-profile.json')))
  observed = File.readlines(File.join(build, 'observed-arguments'), chomp: true)
  recorded = profile.fetch('commands').flat_map { |argv| argv.drop(argv.index('./build.sh') + 1) }
  abort 'FAIL: fixture did not execute the recorded builder commands' unless observed == recorded
  # Do not reconstruct argv from EmberSysroot::FLAGS. The real wrapper generates
  # them, so this pipeline catches disagreements between builder and verifier.
  kit = File.join(dir, 'kit')
  output = run!(RbConfig.ruby, cli, 'prepare', build, kit)
  abort 'FAIL: CLI omitted pending qualification' unless output.include?('qualification: pending')
  run!(RbConfig.ruby, cli, 'verify', kit)
  manifest = EmberSysroot.verify(kit)
  abort 'FAIL: source pin was lost' unless manifest['source_commit'] == pin
  abort 'FAIL: qualification was invented' unless manifest['qualification'].nil?
  puts 'PASS: actual wrapper -> xz sets -> prepare -> verify fixture pipeline'

  clone_build = lambda do |label|
    path = File.join(dir, 'build-' + label)
    FileUtils.cp_r(build, path)
    path
  end
  prepare_refusal = lambda do |label, expected, &change|
    candidate = clone_build.call(label)
    change.call(candidate)
    refuse(label, expected) { EmberSysroot.prepare(candidate, File.join(dir, 'kit-' + label)) }
  end
  prepare_refusal.call('unfinished-build', /build-success|complete/i) do |candidate|
    File.unlink(File.join(candidate, 'build-success'))
  end
  prepare_refusal.call('mixed-source-receipt', /another source/) do |candidate|
    p = JSON.parse(File.read(File.join(candidate, 'build-profile.json')))
    p['source_commit'] = 'b' * 40
    write_json(File.join(candidate, 'build-profile.json'), p)
  end
  prepare_refusal.call('changed-source-export', /Source export differs/) do |candidate|
    File.open(File.join(candidate, 'source.tar.gz'), 'ab') { |file| file.write('changed') }
  end
  prepare_refusal.call('disabled-file-list-check', /profile|override|variable|diagnostic/i) do |candidate|
    p = JSON.parse(File.read(File.join(candidate, 'build-profile.json')))
    p['commands'].each { |argv| argv.insert(-3, '-V', 'NOCHECKFLIST=yes') }
    write_json(File.join(candidate, 'build-profile.json'), p)
  end
  prepare_refusal.call('extra-release-operation', /operation|command|argument|profile/i) do |candidate|
    p = JSON.parse(File.read(File.join(candidate, 'build-profile.json')))
    p['commands'].last.insert(-3, 'release')
    write_json(File.join(candidate, 'build-profile.json'), p)
  end
  archive_refusal = lambda do |label, set, expected, &change|
    prepare_refusal.call(label, expected) do |candidate|
      path = 'release/evbarm-aarch64/binary/sets/' + set
      rewrite_archive(File.join(candidate, path), File.join(candidate, 'rewritten-set'), &change)
      p = JSON.parse(File.read(File.join(candidate, 'build-profile.json')))
      p.fetch('output_sha256')[path] = Digest::SHA256.file(File.join(candidate, path)).hexdigest
      write_json(File.join(candidate, 'build-profile.json'), p)
    end
  end
  {'missing-comp-header' => 'usr/include/stdio.h', 'missing-comp-startup-object' => 'usr/lib/crt0.o',
   'missing-comp-link-library' => 'usr/lib/libc.a'}.each do |label, path|
    archive_refusal.call(label, 'comp.tar.xz', /comp lacks/) do |tree|
      File.unlink(File.join(tree, path))
    end
  end
  prepare_refusal.call('missing-comp-set', /comp.tar.xz/) do |candidate|
    File.unlink(File.join(candidate, 'release/evbarm-aarch64/binary/sets/comp.tar.xz'))
  end
  archive_refusal.call('wrong-startup-architecture', 'comp.tar.xz', /Startup object is not AArch64/) do |tree|
    path = File.join(tree, 'usr/lib/crt0.o')
    bytes = File.binread(path)
    bytes[18, 2] = [62].pack('v') # EM_X86_64, with otherwise unchanged ELF identification.
    File.binwrite(path, bytes)
  end
  archive_refusal.call('native-gcc-command', 'comp.tar.xz', /compiler/) do |tree|
    FileUtils.mkdir_p(File.join(tree, 'usr/bin'))
    File.write(File.join(tree, 'usr/bin/gcc'), 'forbidden fixture command')
  end
  archive_refusal.call('missing-base-loader', 'base.tar.xz', /Base lacks.*loader/) do |tree|
    File.unlink(File.join(tree, 'libexec/ld.elf_so'))
  end
  prepare_refusal.call('wrong-kernel-identity', /kernel lacks.*identity/) do |candidate|
    kernel = File.join(candidate, EmberSysroot::KERNEL)
    bytes = Zlib::GzipReader.open(kernel, &:read).sub("EmberBSD:#{pin}", 'EmberBSD:' + 'b' * 40)
    Zlib::GzipWriter.open(kernel) { |gzip| gzip.write(bytes) }
    p = JSON.parse(File.read(File.join(candidate, 'build-profile.json')))
    p.fetch('output_sha256')[EmberSysroot::KERNEL] = Digest::SHA256.file(kernel).hexdigest
    write_json(File.join(candidate, 'build-profile.json'), p)
  end
  prepare_refusal.call('unsafe-set-path', /Unsafe.*paths/) do |candidate|
    raw = StringIO.new(''.b)
    Gem::Package::TarWriter.new(raw) { |tar| tar.add_file_simple('../outside', 0644, 7) { |file| file.write('fixture') } }
    packed, error, status = Open3.capture3('xz', '-c', stdin_data: raw.string, binmode: true)
    abort error unless status.success?
    path = 'release/evbarm-aarch64/binary/sets/base.tar.xz'
    File.binwrite(File.join(candidate, path), packed)
    p = JSON.parse(File.read(File.join(candidate, 'build-profile.json')))
    p.fetch('output_sha256')[path] = Digest::SHA256.hexdigest(packed)
    write_json(File.join(candidate, 'build-profile.json'), p)
  end

  clone_kit = lambda do |label|
    path = File.join(dir, 'candidate-' + label)
    FileUtils.cp_r(kit, path)
    path
  end
  candidate = clone_kit.call('changed-bytes')
  File.open(File.join(candidate, 'base.tar.xz'), 'ab') { |file| file.write('changed') }
  refuse('changed-set-bytes', /Changed asset/) { EmberSysroot.verify(candidate) }
  candidate = clone_kit.call('extra-file')
  File.write(File.join(candidate, 'private-key.pem'), 'fixture data, not a secret')
  refuse('extra-local-asset', /extra kit file/) { EmberSysroot.verify(candidate) }
  candidate = clone_kit.call('unsafe-asset')
  p = JSON.parse(File.read(File.join(candidate, 'manifest.json')))
  p.fetch('files').first['name'] = '../base.tar.xz'
  write_json(File.join(candidate, 'manifest.json'), p)
  refuse('unsafe-asset-name', /Unsafe.*asset/) { EmberSysroot.verify(candidate) }
  candidate = clone_kit.call('mixed-set-hash')
  reseal_kit(candidate) do |m|
    m['build_profile']['output_sha256']['release/evbarm-aarch64/binary/sets/base.tar.xz'] = '0' * 64
    write_json(File.join(candidate, 'build-profile.json'), m['build_profile'])
  end
  refuse('mixed-set-receipt', /Set belongs to another build/) { EmberSysroot.verify(candidate) }

  calls = []
  refuse('unqualified-publication-before-network', /qualification.*required/i) do
    EmberSysroot::GitHub.new(kit, runner: ->(*args) { calls << args; abort 'FAIL: premature remote request' }).publish
  end
  abort 'FAIL: unqualified publication reached GitHub' unless calls.empty?

  # These declarations test binding and refusal only. They are not runtime
  # results and are removed with this temporary fixture directory.
  hashes = manifest.fetch('files').to_h { |file| [file['name'], file['sha256']] }
  qualification = {'source_commit' => pin, 'base_sha256' => hashes.fetch('base.tar.xz'),
    'comp_sha256' => hashes.fetch('comp.tar.xz'), 'kernel_gzip_sha256' => hashes.fetch(EmberSysroot::KERNEL),
    'consumer_compiler' => 'fixture-only GCC 16.2.0 declaration',
    'execution' => 'EmberBSD aarch64 full-system QEMU TCG',
    'checks' => EmberSysroot::CHECKS.to_h { |name| [name, true] }}
  q_path = File.join(dir, 'qualification-fixture.json')
  write_json(q_path, qualification)
  qualified = File.join(dir, 'qualified-kit')
  EmberSysroot.prepare(build, qualified, q_path)
  EmberSysroot.require_qualification(EmberSysroot.verify(qualified))
  {'source_commit' => 'b' * 40, 'base_sha256' => '0' * 64,
   'comp_sha256' => '0' * 64, 'kernel_gzip_sha256' => '0' * 64,
   'consumer_compiler' => 'fixture GCC 12.5.0', 'execution' => 'Linux host'}.each do |field, value|
    wrong = Marshal.load(Marshal.dump(qualification))
    wrong[field] = value
    write_json(q_path, wrong)
    refuse('mismatched-qualification-' + field, /qualification|Qualification|GCC16|platform/) do
      EmberSysroot.prepare(build, File.join(dir, 'wrong-qualification-' + field), q_path)
    end
  end
  abort 'FAIL: target C11 qualification check is missing from the contract' unless EmberSysroot::CHECKS.include?('target_c11')
  [false, nil].each_with_index do |value, index|
    wrong = Marshal.load(Marshal.dump(qualification))
    value.nil? ? wrong['checks'].delete('target_c11') : wrong['checks']['target_c11'] = value
    write_json(q_path, wrong)
    refuse("unpassed-target-check-#{index}", /Incomplete.*qualification/) do
      EmberSysroot.prepare(build, File.join(dir, "unpassed-target-check-#{index}"), q_path)
    end
  end
end
puts 'PASS: sysroot source, archive composition, byte integrity and target-qualification contract'
