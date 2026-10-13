#!/usr/bin/env ruby
# Origin: EmberBSD - host-file guards for the cable install artifact manifest.
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'

tool = File.join(__dir__, 'a133-install-bundle.rb')
abort 'FAIL: offline bundle checker is missing' unless File.file?(tool)
checks = 0
Dir.mktmpdir('a133-bundle-') do |dir|
  paths = %w[boot.bin resources.bin root.bin].map { |name| File.join(dir, name) }
  paths.zip([33554432, 33554432, 512]).each do |path, size|
    File.open(path, 'wb') { |file| file.truncate(size) }
  end
  manifest = {
    'schema' => 1, 'board' => 'ys-m33-a133', 'source_commit' => 'a' * 40,
    'artifacts' => %w[boot resources root].zip(paths).map do |role, path|
      {'role' => role, 'file' => File.basename(path), 'bytes' => File.size(path),
       'sha256' => Digest::SHA256.file(path).hexdigest}
    end
  }
  input = File.join(dir, 'bundle.json')
  run = lambda do |content, success, reason = nil|
    File.binwrite(input, content)
    before = Dir.children(dir).sort.map do |name|
      path = File.join(dir, name)
      [name, File.lstat(path).ftype,
       File.file?(path) ? Digest::SHA256.file(path).hexdigest : nil]
    end
    out, err, status = Open3.capture3(RbConfig.ruby, tool, input)
    data = JSON.parse(out)
    abort "FAIL: expected #{success ? 'success' : reason}: #{out} #{err}" unless
      status.success? == success && err.empty? && data['writes_performed'] == 0 &&
      data['installation_ready'] == false && !out.include?(dir)
    if success
      abort 'FAIL: missing full manifest receipt' unless data['status'] == 'artifact_manifest_verified' &&
        data['artifacts'].map { |artifact| artifact['role'] }.sort == %w[boot resources root] &&
        data['artifacts'].find { |artifact| artifact['role'] == 'root' }['start_sector'] == 6565888
    else
      abort "FAIL: wrong reason #{data.inspect}" unless data['status'] == 'inspection_stopped' &&
        data['reason'] == reason
    end
    after = Dir.children(dir).sort.map do |name|
      path = File.join(dir, name)
      [name, File.lstat(path).ftype,
       File.file?(path) ? Digest::SHA256.file(path).hexdigest : nil]
    end
    abort 'FAIL: checker changed bundle files' unless before == after
    checks += 1
  end
  good = JSON.generate(manifest)
  run.call(good, true)
  mutate = lambda do |reason, &block|
    candidate = JSON.parse(good)
    block.call(candidate)
    run.call(JSON.generate(candidate), false, reason)
  end
  mutate.call('unsupported_manifest') { |m| m['schema'] = 2 }
  mutate.call('unsupported_manifest') { |m| m['board'] = 'another-a133-tablet' }
  mutate.call('invalid_manifest_fields') { |m| m['credentials'] = 'not-a-release' }
  mutate.call('invalid_source_commit') { |m| m['source_commit'] = 'main' }
  mutate.call('invalid_source_commit') { |m| m['source_commit'] = nil }
  mutate.call('invalid_artifacts') { |m| m['artifacts'] = {} }
  mutate.call('invalid_artifacts') { |m| m['artifacts'].pop }
  mutate.call('invalid_artifact_fields') { |m| m['artifacts'][0]['target'] = '/dev/disk1' }
  mutate.call('invalid_artifact_fields') { |m| m['artifacts'][0] = 1 }
  mutate.call('invalid_roles') { |m| m['artifacts'][0]['role'] = 'env' }
  mutate.call('invalid_roles') { |m| m['artifacts'][1]['role'] = 'boot' }
  mutate.call('invalid_filename') { |m| m['artifacts'][0]['file'] = '../boot.bin' }
  mutate.call('invalid_filename') { |m| m['artifacts'][0]['file'] = paths[0] }
  mutate.call('invalid_filename') { |m| m['artifacts'][0]['file'] = 'x' * 129 }
  mutate.call('duplicate_filename') { |m| m['artifacts'][1]['file'] = 'boot.bin' }
  mutate.call('invalid_sha256') { |m| m['artifacts'][0]['sha256'] = '0' * 63 }
  mutate.call('invalid_image_size') { |m| m['artifacts'][0]['bytes'] = 512 }
  mutate.call('invalid_image_size') { |m| m['artifacts'][2]['bytes'] = 0 }
  mutate.call('invalid_image_size') { |m| m['artifacts'][2]['bytes'] = 513 }
  mutate.call('invalid_image_size') { |m| m['artifacts'][2]['bytes'] = 27676098560 }
  mutate.call('invalid_image_size') { |m| m['artifacts'][2]['bytes'] = '512' }
  mutate.call('image_size_mismatch') { |m| m['artifacts'][2]['bytes'] = 1024 }
  run.call('{', false, 'invalid_json')
  run.call('', false, 'invalid_json')
  run.call('[]', false, 'invalid_manifest_fields')
  run.call(good.sub('"schema":1', '"schema":1,"schema":1'), false, 'duplicate_json_key')
  run.call(good.sub('"role":"boot"', '"role":"boot","role":"boot"'), false, 'duplicate_json_key')
  run.call(' ' * 65537, false, 'manifest_too_large')
  File.open(paths[0], 'r+b') { |file| file.seek(33554431); file.write('x') }
  run.call(good, false, 'image_sha256_mismatch')
  File.open(paths[0], 'r+b') { |file| file.seek(33554431); file.write("\0") }
  File.rename(paths[2], File.join(dir, 'saved-root'))
  run.call(good, false, 'bundle_file_unavailable')
  File.symlink(File.join(dir, 'saved-root'), paths[2])
  run.call(good, false, 'bundle_file_not_regular')
  File.unlink(paths[2])
  File.mkfifo(paths[2])
  run.call(good, false, 'bundle_file_not_regular')
  File.unlink(paths[2])
  File.rename(File.join(dir, 'saved-root'), paths[2])
  File.truncate(paths[2], 1024)
  run.call(good, false, 'image_size_mismatch')
  File.truncate(paths[2], 512)
  File.rename(input, File.join(dir, 'saved-manifest'))
  File.symlink(File.join(dir, 'saved-manifest'), input)
  out, _err, status = Open3.capture3(RbConfig.ruby, tool, input)
  abort 'FAIL: manifest symlink accepted' unless !status.success? &&
    JSON.parse(out)['reason'] == 'bundle_file_not_regular'
  checks += 1
end
puts "Bundle manifest: #{checks} real-file cases passed; no writes or device access"
