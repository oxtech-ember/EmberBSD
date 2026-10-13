# SPDX-License-Identifier: BSD-2-Clause
# Catch partial, corrupted, mixed and unsafe publication inputs.
require 'digest'
require 'json'
require 'tmpdir'
require 'fileutils'
require 'tempfile'

library = File.expand_path('../release/kit.rb', __dir__)
abort 'FAIL: release kit verification is not implemented' unless File.file?(library)
require library

Dir.mktmpdir('ember-release-kit') do |dir|
  sha = 'a' * 40
  names = %w[EmberBSD-1.0-amd64.iso EmberBSD-1.0-amd64-install.img.gz release-notes.md]
  names += %w[base comp etc games gpufw kern-GENERIC man manhtml misc modules rescue tests text].map { |n| "#{n}.tar.xz" }
  digest = 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
  files = names.map do |name|
    File.write(File.join(dir, name), 'abc')
    { 'name' => name, 'size' => 3, 'sha256' => digest }
  end
  manifest = {
    'schema' => 1, 'repository' => 'oxtech-ember/EmberBSD', 'source_commit' => sha,
    'release_tag' => "build-#{sha}-amd64-no-x11", 'architecture' => 'amd64',
    'product_version' => '1.0', 'base_version' => '11.0',
    'profile' => { 'MKX11' => 'no', 'MKRUMP' => 'no', 'MKGCCCMDS' => 'no', 'KERNEL_SETS' => 'GENERIC' },
    'components' => %w[image kernel modules base].to_h { |n| [n, { 'source_commit' => sha }] },
    'files' => files, 'qualification' => nil
  }
  manifest['source_archive_sha256'] = digest
  manifest['components']['kernel'].merge!('buildinfo' => "EmberBSD:#{sha}", 'sha256' => digest)
  seal = lambda do
    File.write(File.join(dir, 'manifest.json'), JSON.pretty_generate(manifest) + "\n")
    sum = files.map { |f| "SHA256 (#{f['name']}) = #{f['sha256']}\n" }.join
    sum += "SHA256 (manifest.json) = #{Digest::SHA256.file(File.join(dir, 'manifest.json')).hexdigest}\n"
    File.write(File.join(dir, 'SHA256'), sum)
  end
  seal.call
  EmberRelease.verify(dir)
  archive_root = File.join(dir, 'archive-input')
  FileUtils.mkdir_p(archive_root)
  File.binwrite(File.join(archive_root, 'payload'), "binary\0payload\n")
  archive = File.join(dir, 'fixture.tar.xz')
  system('tar', '-cJf', archive, '-C', archive_root, 'payload') or abort 'Fixture archive failed'
  destination = File.join(dir, 'extracted')
  EmberRelease.extract(archive, 'payload', destination)
  abort 'FAIL: extraction lost bytes' unless File.binread(destination) == "binary\0payload\n"
  FileUtils.rm_r(archive_root)
  FileUtils.rm([archive, destination])
  Dir.mktmpdir('ember-compiler-set') do |input|
    FileUtils.mkdir_p(File.join(input, 'usr/bin'))
    File.write(File.join(input, 'usr/bin/gcc'), 'fixture')
    bad_archive = File.join(dir, 'compiler.tar.xz')
    system('tar', '-cJf', bad_archive, '-C', input, 'usr') or abort 'Compiler fixture failed'
    begin
      EmberRelease.check_native_compiler(bad_archive)
    rescue EmberRelease::Error
      puts 'PASS: target compiler commands cannot contradict the profile'
    else
      abort 'FAIL: legacy target compiler was accepted'
    end
    File.unlink(bad_archive)
  end
  rejected = lambda do |name, &change|
    change.call
    begin
      EmberRelease.verify(dir)
    rescue EmberRelease::Error
      puts "PASS: rejects #{name}"
    else
      abort "FAIL: accepted #{name}"
    end
  end
  rejected.call('corrupted set') { File.write(File.join(dir, 'base.tar.xz'), 'bad') }
  File.write(File.join(dir, 'base.tar.xz'), 'abc')
  rejected.call('missing set') { File.rename(File.join(dir, 'modules.tar.xz'), File.join(dir, 'saved')) }
  File.rename(File.join(dir, 'saved'), File.join(dir, 'modules.tar.xz'))
  rejected.call('extra private file') { File.write(File.join(dir, 'private-key.pem'), 'fixture') }
  FileUtils.rm(File.join(dir, 'private-key.pem'))
  rejected.call('mixed component source') { manifest['components']['kernel']['source_commit'] = 'b' * 40; seal.call }
  manifest['components']['kernel']['source_commit'] = sha
  rejected.call('duplicate asset') { files << files.first.dup; seal.call }
  files.pop
  rejected.call('unsafe asset path') { files.first['name'] = '../image.iso'; seal.call }
  files.first['name'] = names.first
  seal.call
  Tempfile.create('ember-set') do |external|
    external.write('abc'); external.flush
    File.unlink(File.join(dir, 'base.tar.xz'))
    rejected.call('symlink asset') { File.symlink(external.path, File.join(dir, 'base.tar.xz')) }
    File.unlink(File.join(dir, 'base.tar.xz'))
    File.write(File.join(dir, 'base.tar.xz'), 'abc')
  end
  rejected.call('2 GiB asset') { files.first['size'] = 2**31; seal.call }
  files.first['size'] = 3
  seal.call
  rejected.call('changed checksum manifest') { File.write(File.join(dir, 'SHA256'), "wrong\n") }
  seal.call
  begin
    EmberRelease.require_qualification(manifest)
  rescue EmberRelease::Error
    puts 'PASS: unqualified candidate cannot be published'
  else
    abort 'FAIL: unqualified candidate was publishable'
  end
  EmberRelease.verify(dir)
end
puts 'PASS: complete local kit verification'
