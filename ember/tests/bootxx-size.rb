# SPDX-License-Identifier: BSD-2-Clause
# Replay verified argv without make or writes to the existing object tree.
require 'json'
require 'open3'
require 'digest'
require 'fileutils'
require 'shellwords'
baseline, candidate, compile_json, link_json, objects, work = ARGV
abort 'Usage: ruby bootxx-size.rb BASELINE_C CANDIDATE_C COMPILE_ARGV_JSON LINK_ARGV_JSON OBJECT_DIR NEW_RECEIPT' unless work && ARGV.size == 6
baseline, candidate, compile_json, link_json, objects, work = ARGV.map { |p| File.expand_path(p) }
abort 'Use a new private receipt directory' if File.exist?(work)
FileUtils.mkdir_p(work)
compile = JSON.parse(File.read(compile_json))
link = JSON.parse(File.read(link_json))
cc = compile.first
abort 'Compiler mismatch' unless cc == link.first
run = lambda do |name, argv|
  out, err, status = Open3.capture3(*argv, chdir: objects)
  File.write(work + '/' + name + '.log', out + err)
  abort "#{name} failed; see receipt" unless status.success?
  out
end
abort 'Require the real GNU16 amd64 cross driver' unless run.call('version', [cc, '-dumpfullversion']).strip == '16.2.0' && run.call('target', [cc, '-dumpmachine']).strip == 'x86_64--netbsd'
abort 'Require recorded i386 standalone compile guards' unless %w[-m32 -Os -Werror -ffreestanding -nostdinc -D_STANDALONE -DBOOTXX -c].all? { |v| compile.include?(v) }
abort 'Require one recorded boot1 source and output' unless compile.count { |v| v.end_with?('/boot1.c') } == 1 && compile.count('-o') == 1
abort 'Unexpected compile output side effect' if compile.any? { |v| v.match?(/\A(?:-M|-Wp,-M|-save-temps|@)/) }
abort 'Require the original BIOS linker guards' unless %w[-nostdlib -Wl,-N -Wl,-m,elf_i386 -Wl,-Ttext,0x1000].all? { |v| link.include?(v) } && link.count('-o') == 1 && link.count('-T') == 1 && link.count('boot1.o') == 1 && link.count { |v| v.start_with?('-Wl,-Map,') } == 1
script = link[link.index('-T') + 1]
inputs = [baseline, candidate, compile_json, link_json, cc, script, *link.filter_map { |v| File.expand_path(v, objects) if v.end_with?('.o', '.a') && v != 'boot1.o' }]
# The .d file records the actual configured header context of the failed TU.
if File.file?(objects + '/boot1.d')
  dep = Shellwords.split(File.read(objects + '/boot1.d').gsub(/\\\n/, ' ')).drop(1)
  inputs.concat(dep.map { |p| File.expand_path(p, objects) })
end
inputs.uniq!
hash_inputs = lambda { inputs.to_h { |p| [p, Digest::SHA256.file(p).hexdigest] } }
before = hash_inputs.call
File.write(work + '/inputs-before.json', JSON.pretty_generate(before) + "\n")
objcopy = File.dirname(cc) + '/x86_64--netbsd-objcopy'
%w[baseline candidate revert].each do |name|
  source = name == 'candidate' ? candidate : baseline
  c = compile.map { |v| v.end_with?('/boot1.c') ? source : v }
  c[c.index('-o') + 1] = work + '/' + name + '.o'
  File.write(work + '/' + name + '-compile-argv.json', JSON.pretty_generate(c) + "\n")
  run.call(name + '-compile', c)
  l = link.map { |v| v == 'boot1.o' ? work + '/' + name + '.o' : v.start_with?('-Wl,-Map,') ? '-Wl,-Map,' + work + '/' + name + '.map' : v }
  l[l.index('-o') + 1] = work + '/' + name + '.sym'
  File.write(work + '/' + name + '-link-argv.json', JSON.pretty_generate(l) + "\n")
  run.call(name + '-link', l)
  run.call(name + '-objcopy', [objcopy, '-O', 'binary', work + '/' + name + '.sym', work + '/' + name + '.bin'])
end
abort 'Inputs changed during replay' unless before == hash_inputs.call
old, fixed, reverted = %w[baseline candidate revert].map { |n| File.binread(work + '/' + n + '.bin') }
abort 'Baseline did not reproduce the unchanged 8192-byte failure' unless old.size > 8192
abort 'Candidate exceeds the unchanged 8192-byte limit' unless fixed.size <= 8192
abort 'Revert control did not restore identical oversized bytes' unless old == reverted
abort 'PBR/disklabel sectors changed' unless old[0, 1024] == fixed[0, 1024]
abort 'Missing BIOS signature' unless fixed[510, 2] == "\x55\xaa".b
padded = fixed + "\0" * ((512 - fixed.size % 512) % 512)
abort 'Sector padding exceeds the unchanged limit' unless padded.size <= 8192
File.binwrite(work + '/bootxx_ustarfs', padded)
File.write(work + '/receipt.json', JSON.pretty_generate({scope: 'isolated compile/relink only; no BIOS/VM execution', inputs: before, baseline_size: old.size, candidate_size: fixed.size, padded_size: padded.size, max_size: 8192, baseline_sha256: Digest::SHA256.hexdigest(old), candidate_sha256: Digest::SHA256.hexdigest(fixed), revert: 'byte-identical oversized baseline; rejected by unchanged size gate', pbr_disklabel: 'first 1024 bytes unchanged'}) + "\n")
puts "PASS baseline #{old.size} >8192; candidate #{fixed.size} <=8192; revert rejected; PBR/disklabel unchanged"
