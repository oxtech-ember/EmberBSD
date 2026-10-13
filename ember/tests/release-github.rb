# SPDX-License-Identifier: BSD-2-Clause
# An API fixture is required to test refusal paths without changing GitHub.
require 'json'
require 'tmpdir'
require 'digest'
library = File.expand_path('../release/github.rb', __dir__)
abort 'FAIL: GitHub publisher is not implemented' unless File.file?(library)
require library

Dir.mktmpdir('ember-github') do |dir|
  sha = 'a' * 40
  names = %w[EmberBSD-1.0-amd64.iso EmberBSD-1.0-amd64-install.img.gz release-notes.md]
  names += %w[base comp etc games gpufw kern-GENERIC man manhtml misc modules rescue tests text].map { |s| "#{s}.tar.xz" }
  hash = 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
  names.each { |n| File.write(File.join(dir, n), 'abc') }
  manifest = { 'schema' => 1, 'repository' => 'oxtech-ember/EmberBSD', 'source_commit' => sha,
    'release_tag' => "build-#{sha}-amd64-no-x11", 'source_archive_sha256' => hash,
    'architecture' => 'amd64', 'product_version' => '1.0', 'base_version' => '11.0',
    'profile' => { 'MKX11' => 'no', 'MKRUMP' => 'no', 'MKGCCCMDS' => 'no', 'KERNEL_SETS' => 'GENERIC' },
    'components' => %w[image kernel modules base].to_h { |n| [n, { 'source_commit' => sha }] },
    'files' => names.map { |n| { 'name' => n, 'size' => 3, 'sha256' => hash } }, 'qualification' => nil }
  manifest['components']['kernel'].merge!('sha256' => hash, 'buildinfo' => "EmberBSD:#{sha}")
  File.write(File.join(dir, 'manifest.json'), JSON.pretty_generate(manifest) + "\n")
  sum = names.map { |n| "SHA256 (#{n}) = #{hash}\n" }.join
  sum += "SHA256 (manifest.json) = #{Digest::SHA256.file(File.join(dir, 'manifest.json')).hexdigest}\n"
  File.write(File.join(dir, 'SHA256'), sum)

  # gh api returns nonzero for a valid HTTP 404. Only the optional tag probe
  # can treat that HTTP result as absence; transport failures remain errors.
  fake = File.join(dir, 'gh')
  File.write(fake, "#!/bin/sh\nprintf 'HTTP/2.0 404 Not Found\\r\\n\\r\\n{\"message\":\"Not Found\"}'\nexit 1\n")
  File.chmod(0755, fake)
  # Keep the fixture command outside the sealed kit.
  commands = File.join(File.dirname(dir), File.basename(dir) + '-commands')
  Dir.mkdir(commands)
  File.rename(fake, File.join(commands, 'gh'))
  original_path = ENV['PATH']
  begin
    ENV['PATH'] = commands + ':' + original_path
    client = EmberRelease::GitHub.new(dir)
    abort 'FAIL: missing draft was not recognized' unless client.api('releases/tags/missing', missing: true).nil?
    begin
      client.api('immutable-releases')
    rescue EmberRelease::Error
      puts 'PASS: optional 404 is absence; required API errors stop'
    else
      abort 'FAIL: required 404 was accepted'
    end
  ensure
    ENV['PATH'] = original_path
    File.unlink(File.join(commands, 'gh'))
    Dir.rmdir(commands)
  end

  %w[disabled diverged foreign-tag published extra-asset].each do |scenario|
    calls = []
    runner = lambda do |*args|
      calls << args
      abort 'FAIL: refusal performed a mutation' unless args.first == 'api'
      endpoint = args.last
      data = case endpoint
      when /immutable-releases$/ then { 'enabled' => scenario != 'disabled' }
      when /compare\// then { 'status' => scenario == 'diverged' ? 'diverged' : 'ahead' }
      when /matching-refs/ then [{ 'ref' => "refs/tags/#{manifest['release_tag']}", 'object' => { 'type' => 'commit', 'sha' => scenario == 'foreign-tag' ? 'b' * 40 : sha } }]
      when /releases\/tags\// then { 'draft' => scenario != 'published', 'prerelease' => true,
        'name' => "EmberBSD 1.0 amd64 #{sha[0, 12]} (no X11 VM candidate)", 'body' => 'abc',
        'assets' => scenario == 'extra-asset' ? [{ 'name' => 'secret.pem', 'state' => 'uploaded', 'size' => 3, 'digest' => "sha256:#{hash}" }] : [] }
      else abort "Unexpected API: #{endpoint}"
      end
      "HTTP/2.0 200 OK\r\n\r\n#{JSON.generate(data)}"
    end
    begin
      EmberRelease::GitHub.new(dir, runner: runner).draft
    rescue EmberRelease::Error
      puts "PASS: #{scenario} stops before a GitHub mutation"
    else
      abort "FAIL: #{scenario} was accepted"
    end
    abort 'FAIL: API preflight was not exercised' if calls.empty?
  end
  # A draft can reserve its tag before GitHub creates the published Git ref.
  pending_runner = lambda do |*args|
    endpoint = args.last
    data = case endpoint
    when /immutable-releases$/ then { 'enabled' => true }
    when /compare\// then { 'status' => 'ahead' }
    when /matching-refs/ then []
    when /releases\/tags\// then { 'draft' => true, 'target_commitish' => sha, 'assets' => [] }
    else abort "Unexpected pending API: #{endpoint}"
    end
    "HTTP/2.0 200 OK\r\n\r\n#{JSON.generate(data)}"
  end
  result = EmberRelease::GitHub.new(dir, runner: pending_runner).preflight
  abort 'FAIL: exact source draft rejected before its tag is published' unless result['target_commitish'] == sha
  puts 'PASS: pending draft tag remains bound to the full source commit'
  calls = []
  begin
    EmberRelease::GitHub.new(dir, runner: ->(*args) { calls << args }).publish
  rescue EmberRelease::Error
    abort 'FAIL: unqualified publication made a remote call' unless calls.empty?
    puts 'PASS: unqualified publication stops locally'
  else
    abort 'FAIL: unqualified publication accepted'
  end

  q = { 'source_commit' => sha, 'image_sha256' => hash, 'usb_image_sha256' => hash,
    'installed_kernel_sha256' => hash, 'checks' => EmberRelease::VM_CHECKS.to_h { |n| [n, true] } }
  File.write(File.join(dir, 'qualification.json'), JSON.pretty_generate(q) + "\n")
  manifest['qualification'] = q
  manifest['files'] << { 'name' => 'qualification.json', 'size' => File.size(File.join(dir, 'qualification.json')),
    'sha256' => Digest::SHA256.file(File.join(dir, 'qualification.json')).hexdigest }
  File.write(File.join(dir, 'manifest.json'), JSON.pretty_generate(manifest) + "\n")
  File.write(File.join(dir, 'SHA256'), EmberRelease.checksum_text(dir, manifest['files'].map { |f| f['name'] } + ['manifest.json']))
  %w[name body prerelease].each do |field|
    mutated = false
    runner = lambda do |*args|
      if args.first != 'api'
        mutated = true if args[0, 2] == %w[release edit]
        if args[0, 2] == %w[release download]
          destination = args[args.index('--dir') + 1]
          Dir.children(dir).each { |n| FileUtils.cp(File.join(dir, n), File.join(destination, n)) }
        end
        next ''
      end
      data = case args.last
      when /immutable-releases$/ then { 'enabled' => true }
      when /compare\// then { 'status' => 'ahead' }
      when /matching-refs/ then [{ 'ref' => "refs/tags/#{manifest['release_tag']}", 'object' => { 'type' => 'commit', 'sha' => sha } }]
      when /releases\/tags\//
        release = { 'draft' => !mutated, 'immutable' => mutated, 'prerelease' => true,
          'name' => "EmberBSD 1.0 amd64 #{sha[0, 12]} (no X11 VM candidate)", 'body' => 'abc',
          'assets' => Dir.children(dir).map { |n| { 'name' => n, 'state' => 'uploaded',
            'size' => File.size(File.join(dir, n)), 'digest' => "sha256:#{Digest::SHA256.file(File.join(dir, n)).hexdigest}" } } }
        release[field] = field == 'prerelease' ? false : 'Foreign unverified metadata'
        release
      else abort "Unexpected publish API: #{args.last}"
      end
      "HTTP/2.0 200 OK\r\n\r\n#{JSON.generate(data)}"
    end
    begin
      EmberRelease::GitHub.new(dir, runner: runner).publish
    rescue EmberRelease::Error => e
      abort "FAIL: wrong metadata error: #{e.message}" unless e.message == 'Existing draft does not belong to this kit'
    else
      abort "FAIL: foreign draft #{field} was published"
    end
    abort 'FAIL: draft metadata was checked after publication' if mutated
    puts "PASS: changed draft #{field} stops publication"
  end
end
