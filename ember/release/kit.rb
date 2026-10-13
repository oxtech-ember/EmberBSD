# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'json'
require 'open3'
require 'fileutils'
require 'zlib'
require 'stringio'

module EmberRelease
  class Error < StandardError; end
  REPOSITORY = 'oxtech-ember/EmberBSD'.freeze
  SETS = %w[base comp etc games gpufw kern-GENERIC man manhtml misc modules rescue tests text].freeze
  OPTIONAL_SETS = %w[base32 comp32 debug debug32 debug64].freeze
  IMAGES = %w[EmberBSD-1.0-amd64.iso EmberBSD-1.0-amd64-install.img.gz].freeze
  VM_CHECKS = %w[local_install https_install first_boot_without_media reboot_persistence
    tls_reject_unknown_ca tls_reject_wrong_name tls_reject_missing_anchors http_404_rejected github_redirect].freeze

  def self.check(condition, message)
    raise Error, message unless condition
  end

  def self.command(*args)
    out, err, status = Open3.capture3(*args, binmode: true)
    check(status.success?, "#{args.first}: #{err.force_encoding('UTF-8')}")
    out
  end

  def self.tag(sha)
    "build-#{sha}-amd64-no-x11"
  end

  def self.extract(archive, path, destination)
    File.open(destination, 'wx') do |file|
      statuses = Open3.pipeline(['tar', '-xOf', archive, path], out: file)
      check(statuses.all?(&:success?), "Cannot extract #{path}")
    end
  end

  def self.check_native_compiler(archive)
    names = command('tar', '-tf', archive).force_encoding('UTF-8').lines.map { |n| n.strip.delete_prefix('./') }
    forbidden = %w[usr/bin/cc usr/bin/c++ usr/bin/gcc usr/bin/g++ usr/bin/cpp]
    check((names & forbidden).empty? && names.none? { |n| n.match?(%r{\Ausr/libexec/(?:gcc/.*?/)?cc1(?:plus|obj)?\z}) },
      'comp contains native bootstrap compiler commands')
  end

  def self.checksum_text(dir, files)
    files.map { |name| "SHA256 (#{name}) = #{Digest::SHA256.file(File.join(dir, name)).hexdigest}\n" }.join
  end

  def self.verify(dir)
    check(File.directory?(dir) && !File.symlink?(dir), 'Use a real kit directory')
    %w[manifest.json SHA256].each do |name|
      path = File.join(dir, name)
      check(File.file?(path) && !File.symlink?(path), "Missing regular #{name}")
    end
    m = JSON.parse(File.read(File.join(dir, 'manifest.json')))
    sha = m['source_commit']
    check(m['schema'] == 1 && sha.is_a?(String) && sha.match?(/\A[0-9a-f]{40}\z/), 'Invalid source identity')
    check(m['repository'] == REPOSITORY && m['release_tag'] == tag(sha), 'Wrong repository or release tag')
    check(m.values_at('architecture', 'product_version', 'base_version') == %w[amd64 1.0 11.0], 'Wrong target or version')
    check(m.fetch('profile').values_at('MKX11', 'MKRUMP', 'MKGCCCMDS', 'KERNEL_SETS') == %w[no no no GENERIC], 'Wrong release profile')
    check(m['source_archive_sha256'].is_a?(String) && m['source_archive_sha256'].match?(/\A[0-9a-f]{64}\z/), 'Missing source archive hash')
    %w[image kernel modules base].each do |name|
      check(m.fetch('components').fetch(name)['source_commit'] == sha, "Mixed #{name} source")
    end
    kernel = m['components']['kernel']
    check(kernel['buildinfo'] == "EmberBSD:#{sha}" && kernel['sha256'].to_s.match?(/\A[0-9a-f]{64}\z/), 'Invalid kernel identity')
    files = m.fetch('files')
    check(files.is_a?(Array) && files.all? { |f| f.is_a?(Hash) }, 'Invalid asset list')
    names = files.map { |f| f['name'] }
    check(names.all? { |n| n.is_a?(String) && n.match?(/\A[A-Za-z0-9][A-Za-z0-9._-]*\z/) }, 'Unsafe asset name')
    check(names.uniq == names && !(names & %w[manifest.json SHA256]).any?, 'Duplicate or reserved asset')
    required = IMAGES + ['release-notes.md'] + SETS.map { |n| "#{n}.tar.xz" }
    check((required - names).empty?, 'Incomplete installation profile')
    allowed = required + OPTIONAL_SETS.map { |n| "#{n}.tar.xz" } + %w[SHA512 qualification.json]
    check((names - allowed).empty?, 'Unexpected asset for this profile')
    check(Dir.children(dir).sort == (names + %w[manifest.json SHA256]).sort, 'Missing or extra kit file')
    files.each do |f|
      path = File.join(dir, f['name'])
      check(File.file?(path) && !File.symlink?(path), "Not a regular asset: #{f['name']}")
      check(f['size'].is_a?(Integer) && f['size'] > 0 && f['size'] < 2**31, "Invalid GitHub asset size: #{f['name']}")
      check(File.size(path) == f['size'] && Digest::SHA256.file(path).hexdigest == f['sha256'], "Corrupted asset: #{f['name']}")
    end
    check(File.read(File.join(dir, 'SHA256')) == checksum_text(dir, names + ['manifest.json']), 'Checksum manifest changed')
    if m['qualification']
      check(names.include?('qualification.json'), 'Missing qualification receipt')
      check(JSON.parse(File.read(File.join(dir, 'qualification.json'))) == m['qualification'], 'Qualification receipt changed')
      require_qualification(m)
    else
      check(!names.include?('qualification.json'), 'Unbound qualification receipt')
    end
    m
  rescue JSON::ParserError, KeyError, TypeError, NoMethodError, Errno::ENOENT => e
    raise Error, "Invalid kit: #{e.message}"
  end

  def self.require_qualification(m)
    q = m['qualification']
    check(q.is_a?(Hash), 'VM qualification is required before publication')
    check(q['source_commit'] == m['source_commit'], 'Qualification belongs to another source')
    image = m['files'].find { |f| f['name'] == IMAGES.first }
    check(q['image_sha256'] == image['sha256'], 'Qualification belongs to another image')
    usb = m['files'].find { |f| f['name'] == IMAGES.last }
    check(q['usb_image_sha256'] == usb['sha256'], 'Qualification belongs to another USB image')
    check(q['installed_kernel_sha256'] == m['components']['kernel']['sha256'], 'Qualification has another kernel')
    check(VM_CHECKS.all? { |name| q.fetch('checks', {})[name] == true }, 'Incomplete VM qualification')
  end

  def self.prepare(build, dir, qualification = nil)
    sha = File.read(File.join(build, 'build-success')).strip
    check(sha.match?(/\A[0-9a-f]{40}\z/), 'No complete pinned build')
    source = File.join(build, 'source')
    iso = File.join(build, 'release/amd64/installation/cdrom/amd64cd.iso')
    usb = File.join(build, 'release/amd64/installation/installimage', IMAGES.last)
    command('ruby', File.join(source, 'ember/tests/installer-image.rb'), iso, sha, '1.0', 'github.com')
    list = command('tar', '-tf', iso).force_encoding('UTF-8').lines.map(&:strip)
    prefix = 'amd64/binary/sets/'
    paths = list.select { |n| n.start_with?(prefix) && n.end_with?('.tar.xz') }
    names = paths.map { |n| n.delete_prefix(prefix) }
    required = SETS.map { |n| "#{n}.tar.xz" }
    allowed = required + OPTIONAL_SETS.map { |n| "#{n}.tar.xz" }
    check((required - names).empty? && (names - allowed).empty? && names.uniq == names, 'ISO has incomplete or foreign sets')
    sums = command('tar', '-xOf', iso, prefix + 'SHA512').force_encoding('UTF-8')
    sha512 = sums.scan(/^SHA512 \(([^)]+)\)\s*=\s*([0-9a-f]{128})$/).to_h
    check(names.all? { |n| sha512.key?(n) }, 'ISO lacks set checksums')
    Dir.mkdir(dir, 0700)
    paths.each do |path|
      destination = File.join(dir, path.delete_prefix(prefix))
      extract(iso, path, destination)
      check(Digest::SHA512.file(destination).hexdigest == sha512[File.basename(destination)], "ISO set checksum failed: #{path}")
    end
    kernel = command('tar', '-xOf', File.join(dir, 'kern-GENERIC.tar.xz'), './netbsd')
    check_native_compiler(File.join(dir, 'comp.tar.xz'))
    buildinfo = "EmberBSD:#{sha}"
    check(kernel.include?(buildinfo + "\0"), 'Installed kernel does not report the full build source')
    boot = command('tar', '-xOf', iso, 'netbsd')
    boot = Zlib::GzipReader.new(StringIO.new(boot)).read if boot.start_with?("\x1f\x8b".b)
    kernel_sha = Digest::SHA256.hexdigest(kernel)
    check(Digest::SHA256.hexdigest(boot) == kernel_sha, 'ISO boot kernel differs from its installation set')
    FileUtils.cp(iso, File.join(dir, IMAGES.first))
    command('gzip', '-t', usb)
    FileUtils.cp(usb, File.join(dir, IMAGES.last))
    File.write(File.join(dir, 'SHA512'), sums)
    q = qualification ? JSON.parse(File.read(qualification)) : nil
    if q
      q = q.slice('source_commit', 'image_sha256', 'usb_image_sha256', 'installed_kernel_sha256', 'checks')
      q['checks'] = q.fetch('checks').slice(*VM_CHECKS)
      File.write(File.join(dir, 'qualification.json'), JSON.pretty_generate(q) + "\n")
    end
    note = <<~TEXT
      # EmberBSD 1.0: amd64 UEFI installation candidate

      Source: `#{sha}`. Compatibility base: NetBSD 11.0.
      Profile: no X11, no rump, GENERIC kernel and matching modules/base.
      The build uses the in-tree host bootstrap compiler. It does not establish a complete GCC16 userland.
      Native GCC12 commands are excluded. Modern native development tools belong to the Ports profile.

      #{q ? 'UEFI local installation, HTTPS installation and certificate rejection passed in an isolated QEMU VM.' : 'VM qualification is pending. Do not publish this candidate.'}
      Physical amd64 machines, AArch64 boards and Orange Pi 6 Plus are not accepted by this candidate.
      GitHub-native full installation needs its separate post-publication check.

      Download the full ISO or the compressed UEFI USB image. Verify SHA256 before use.
      Back up the destination before writing media or installing.
      The installer uses `https://github.com/#{REPOSITORY}/releases/download/#{tag(sha)}/` for binary sets.
      Source archives are not included in this binary set channel.
    TEXT
    File.write(File.join(dir, 'release-notes.md'), note)
    assets = Dir.children(dir).sort.map do |name|
      path = File.join(dir, name)
      { 'name' => name, 'size' => File.size(path), 'sha256' => Digest::SHA256.file(path).hexdigest }
    end
    compiler = command(File.join(build, 'tools/bin/x86_64--netbsd-gcc'), '--version').lines.first.strip
    components = %w[image kernel modules base].to_h { |n| [n, { 'source_commit' => sha }] }
    components['kernel'].merge!('buildinfo' => buildinfo, 'sha256' => kernel_sha)
    manifest = { 'schema' => 1, 'repository' => REPOSITORY, 'source_commit' => sha,
      'source_archive_sha256' => Digest::SHA256.file(File.join(build, 'source.tar')).hexdigest,
      'release_tag' => tag(sha), 'architecture' => 'amd64', 'product_version' => '1.0', 'base_version' => '11.0',
      'profile' => { 'MKX11' => 'no', 'MKRUMP' => 'no', 'MKGCCCMDS' => 'no', 'MKREPRO' => 'yes', 'KERNEL_SETS' => 'GENERIC',
        'BUILD_KERNELS' => 'INSTALL', 'compiler' => compiler, 'trust_store' => 'pinned Mozilla server.trust' },
      'components' => components, 'files' => assets, 'qualification' => q,
      'omitted_sets' => sha512.keys - names, 'hardware_qualification' => 'not-tested' }
    File.write(File.join(dir, 'manifest.json'), JSON.pretty_generate(manifest) + "\n")
    File.write(File.join(dir, 'SHA256'), checksum_text(dir, assets.map { |f| f['name'] } + ['manifest.json']))
    verify(dir)
  rescue Errno::ENOENT, Errno::EEXIST => e
    raise Error, "Preparation stopped; do not publish partial output: #{e.message}"
  end
end
