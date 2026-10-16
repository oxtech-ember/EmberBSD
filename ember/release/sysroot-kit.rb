# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
require_relative 'github'

module EmberSysroot
  extend self
  SCHEMA = 'emberbsd.aarch64.sysroot.v1'.freeze
  SETS = %w[base.tar.xz comp.tar.xz].freeze
  KERNEL = 'netbsd-EMBER64.gz'.freeze
  EVIDENCE = 'target-acceptance.log'.freeze
  REQUIRED = (SETS + [KERNEL, 'build-profile.json', 'source-export.sha256', 'release-notes.md']).freeze
  CHECKS = %w[libc_aliases cross_c11 cross_cxx20 target_c11 target_cxx20 base_loader_smoke fork_cas].freeze
  FLAGS = %w[USETOOLS=yes HAVE_GCC=12 TOOLCHAIN_MISSING=no MKGCC=yes MKGCCCMDS=no MKCXX=yes
    MKLIBSTDCXX=yes MKLINKLIB=yes MKSTATICLIB=yes MKPIC=yes MKPICLIB=yes MKDYNAMICROOT=yes
    MKFIRMWARE=yes MKKMOD=no MKX11=no MKRUMP=no MKCOMPAT=no MKLLVM=no MKLLVMRT=no
    MKDEBUG=no MKDEBUGLIB=no MKDEBUGKERNEL=no MKATF=no MKKYUA=no MKMAN=no MKDOC=no
    MKINFO=no MKHTML=no MKLINT=no MKPROFILE=no MKCVS=no MKGROFF=no MKGDB=no MKBINUTILS=no
    MKREPRO=yes RELEASEMACHINEDIR=evbarm-aarch64 USE_XZ_SETS=yes MAKETARSETS=base\ comp
    EMBER_VERSION=1.0].freeze

  def check(condition, message)
    EmberRelease.check(condition, message)
  end

  def tag(sha)
    "build-#{sha}-aarch64-sysroot"
  end

  def sha256(path)
    Digest::SHA256.file(path).hexdigest
  end

  def profile_check(profile, sha)
    check(profile.values_at('schema', 'status', 'machine', 'architecture', 'kernel_config') ==
      [1, 'built', 'evbarm', 'aarch64', 'EMBER64'], 'Incomplete or wrong AArch64 build profile')
    check(profile['source_repository'] == 'https://github.com/oxtech-ember/EmberBSD' &&
      profile['source_commit'] == sha, 'Build receipt belongs to another source')
    check(profile['source_sha256'].is_a?(String) && profile['source_sha256'].match?(/\A[0-9a-f]{64}\z/), 'Missing source export hash')
    check(profile['source_timestamp'].is_a?(Integer) && profile['source_timestamp'].positive?, 'Invalid source timestamp')
    check(profile['target_cc_version'].is_a?(String) && profile['target_cc_version'].include?('12.5.0'), 'Unrecorded bootstrap compiler')
    commands = profile.fetch('commands')
    check(commands.is_a?(Array) && commands.length == 2, 'Missing full build commands')
    commands.each_with_index do |argv, index|
      check(argv.is_a?(Array) && argv.all? { |value| value.is_a?(String) }, 'Invalid build argv')
      check(argv.take(2) == %w[env -i] && argv.include?('MAKECONF=/dev/null') && argv.include?('./build.sh'), 'Uncontrolled build environment')
      variables = argv.each_cons(2).select { |a, _| a == '-V' }.map(&:last)
      expected = FLAGS + ["MKREPRO_TIMESTAMP=#{profile['source_timestamp']}", "EMBER_BUILD_ID=#{sha}", "BUILDINFO=EmberBSD:#{sha}"]
      obj_index = argv.index('-O')
      check(obj_index && argv[obj_index + 1]&.start_with?('/'), 'Missing object directory')
      expected += ["HOST_CC=#{profile.fetch('host').fetch('cc')}", "HOST_CXX=#{profile.fetch('host').fetch('cxx')}",
        "KERNOBJDIR=#{argv[obj_index + 1]}/kernels"]
      check(variables.sort == expected.sort, 'Build command does not select the exact required profile')
      check(variables.map { |v| v.split('=', 2).first }.uniq.length == variables.length, 'Conflicting build variable overrides')
      check(argv.include?('-U') && argv.include?('-u') && argv.each_cons(2).include?(%w[-m evbarm]) &&
        argv.each_cons(2).include?(%w[-a aarch64]), 'Wrong cross-build target')
      check(argv.last(2) == (index.zero? ? %w[tools kernel=EMBER64] : %w[distribution sets]), 'Incomplete build stages')
      environment = profile.fetch('build_environment')
      check(environment.keys.sort == %w[PATH HOME TMPDIR LC_ALL MAKECONF].sort && environment['LC_ALL'] == 'C' &&
        environment['MAKECONF'] == '/dev/null', 'Unexpected build environment')
      prefix = %w[env -i] + %w[PATH HOME TMPDIR LC_ALL MAKECONF].map { |name| "#{name}=#{environment.fetch(name)}" } + %w[sh ./build.sh]
      check(argv.take(prefix.length) == prefix, 'Build command contains extra inherited controls')
      arguments = argv.drop(prefix.length)
      check(arguments.take(6) == %w[-U -u -m evbarm -a aarch64] && arguments[6].to_s.match?(/\A-j[1-9][0-9]*\z/), 'Unexpected build options')
      check([7, 9, 11, 13].map { |i| arguments[i] } == %w[-O -T -D -R], 'Unexpected output-directory options')
      directories = [8, 10, 12, 14].map { |i| arguments[i] }
      check(directories.all? { |value| value.is_a?(String) && value.start_with?('/') && !value.match?(/\s/) }, 'Invalid output directories')
      output = File.dirname(directories[0])
      check(directories == %w[obj tools dest release].map { |name| output + '/' + name }, 'Mixed output directories')
      controls = arguments[15...-2]
      check(controls && controls.length == expected.length * 2 && controls.each_slice(2).all? { |key, _| key == '-V' }, 'Unexpected build operations or controls')
    end
  end

  def archive_names(path)
    names = EmberRelease.command('tar', '-tf', path).force_encoding('UTF-8').lines.map(&:strip)
    check(!names.empty? && names.all? do |name|
      !name.start_with?('/') && !name.split('/').include?('..')
    end, 'Unsafe or empty release-set paths')
    names
  end

  def elf_aarch64?(bytes)
    bytes = bytes.b
    bytes.byteslice(0, 6) == "\x7fELF\x02\x01".b && bytes.byteslice(18, 2) == [183].pack('v')
  end

  def set_content_check(dir)
    base = File.join(dir, SETS[0])
    comp = File.join(dir, SETS[1])
    base_names, comp_names = [base, comp].map { |path| archive_names(path) }
    normalized = comp_names.map { |p| p.delete_prefix('./') }
    required = %w[usr/include/stdio.h usr/include/stdlib.h usr/include/pthread.h usr/lib/crt0.o usr/lib/crti.o
      usr/lib/crtn.o usr/lib/libc.a usr/lib/libm.a usr/lib/libpthread.a]
    check((required - normalized).empty?, 'comp lacks required headers, startup objects or link libraries')
    EmberRelease.check_native_compiler(comp)
    crt = comp_names.find { |p| p.delete_prefix('./') == 'usr/lib/crt0.o' }
    check(elf_aarch64?(EmberRelease.command('tar', '-xOf', comp, crt)), 'Startup object is not AArch64 ELF64 little-endian')
    loader = base_names.find { |p| p.delete_prefix('./') == 'libexec/ld.elf_so' }
    check(loader && elf_aarch64?(EmberRelease.command('tar', '-xOf', base, loader)), 'Base lacks the AArch64 ELF loader')
    libc = base_names.select { |p| p.match?(%r{\A(?:\./)?(?:usr/)?lib/libc\.so\.12\.\d+\z}) }
    check(!libc.empty? && libc.any? { |p| elf_aarch64?(EmberRelease.command('tar', '-xOf', base, p)) }, 'Base lacks an AArch64 libc provider')
  end

  def kernel_check(path, sha)
    bytes = Zlib::GzipReader.open(path, &:read)
    check(elf_aarch64?(bytes), 'Test kernel is not AArch64 ELF64 little-endian')
    check(bytes.include?("EmberBSD:#{sha}\n\0"), 'Test kernel lacks the full source build identity')
  rescue Zlib::Error => e
    raise EmberRelease::Error, "Invalid kernel gzip: #{e.message}"
  end

  def require_qualification(manifest)
    q = manifest['qualification']
    check(q.is_a?(Hash) && q['source_commit'] == manifest['source_commit'], 'A matching target qualification is required')
    {'base_sha256' => SETS[0], 'comp_sha256' => SETS[1], 'kernel_gzip_sha256' => KERNEL}.each do |field, name|
      check(q[field] == manifest.fetch('files').find { |file| file['name'] == name }.fetch('sha256'), 'Qualification has another ' + name)
    end
    check(CHECKS.all? { |name| q.fetch('checks', {})[name] == true }, 'Incomplete sysroot target qualification')
    check(q['consumer_compiler'].is_a?(String) && q['consumer_compiler'].include?('16.2.0'), 'Qualification must identify actual GCC16 consumers')
    check(q['execution'] == 'EmberBSD aarch64 full-system QEMU TCG', 'Unrecorded target execution platform')
    evidence = manifest.fetch('files').find { |file| file['name'] == EVIDENCE }
    check(evidence && q.fetch('evidence_sha256', {})[EVIDENCE] == evidence['sha256'], 'Qualification lacks bound target evidence')
  end

  def verify(dir)
    check(File.directory?(dir) && !File.symlink?(dir), 'Use a real kit directory')
    %w[manifest.json SHA256].each { |name| check(File.file?(File.join(dir, name)) && !File.symlink?(File.join(dir, name)), 'Missing regular ' + name) }
    manifest = JSON.parse(File.read(File.join(dir, 'manifest.json')))
    sha = manifest['source_commit']
    check(sha.is_a?(String) && sha.match?(/\A[0-9a-f]{40}\z/), 'Invalid full source commit')
    check(manifest.values_at('schema', 'repository', 'release_tag', 'architecture', 'machine', 'product_version', 'base_version') ==
      [SCHEMA, EmberRelease::REPOSITORY, tag(sha), 'aarch64', 'evbarm', '1.0', '11.0'], 'Wrong sysroot identity')
    files = manifest.fetch('files')
    check(files.is_a?(Array) && files.all? { |file| file.is_a?(Hash) }, 'Invalid files manifest')
    names = files.map { |file| file['name'] }
    check(names.all? { |name| name.is_a?(String) && name.match?(/\A[A-Za-z0-9][A-Za-z0-9._-]*\z/) } && names.uniq == names, 'Unsafe or duplicate asset')
    check((REQUIRED - names).empty? && (names - REQUIRED - ['qualification.json', 'build-run.json', EVIDENCE]).empty?, 'Incomplete or foreign sysroot asset set')
    check(Dir.children(dir).sort == (names + %w[manifest.json SHA256]).sort, 'Missing or extra kit file')
    files.each do |file|
      path = File.join(dir, file.fetch('name'))
      check(File.file?(path) && !File.symlink?(path) && file['size'].is_a?(Integer) && file['size'].positive? &&
        file['size'] < 2**31 && File.size(path) == file['size'] && sha256(path) == file['sha256'], 'Changed asset: ' + file['name'])
    end
    check(File.read(File.join(dir, 'SHA256')) == EmberRelease.checksum_text(dir, names + ['manifest.json']), 'Checksum manifest changed')
    profile = JSON.parse(File.read(File.join(dir, 'build-profile.json')))
    profile_check(profile, sha)
    check(manifest['source_archive_sha256'] == profile['source_sha256'] && manifest['build_profile'] == profile, 'Source/build receipt changed')
    check(File.read(File.join(dir, 'source-export.sha256')) == "#{profile['source_sha256']}  source.tar.gz\n", 'Source export hash receipt changed')
    SETS.each do |name|
      hash = profile.fetch('output_sha256').fetch('release/evbarm-aarch64/binary/sets/' + name)
      check(hash == files.find { |file| file['name'] == name }.fetch('sha256'), 'Set belongs to another build: ' + name)
    end
    check(profile.fetch('output_sha256').fetch(KERNEL) == sha256(File.join(dir, KERNEL)), 'Kernel belongs to another build')
    if manifest['qualification']
      check(names.include?('qualification.json') && JSON.parse(File.read(File.join(dir, 'qualification.json'))) == manifest['qualification'], 'Qualification receipt changed')
      require_qualification(manifest)
    else
      check(!names.include?('qualification.json') && !names.include?(EVIDENCE), 'Unbound qualification receipt or target evidence')
    end
    if names.include?('build-run.json')
      run = JSON.parse(File.read(File.join(dir, 'build-run.json')))
      check(run['headSha'] == sha && run['conclusion'] == 'success' && run['url'].to_s.match?(%r{\Ahttps://github\.com/oxtech-ember/EmberBSD/actions/runs/[0-9]+\z}), 'CI receipt belongs to another build')
    end
    kernel_check(File.join(dir, KERNEL), sha)
    set_content_check(dir)
    manifest
  rescue JSON::ParserError, KeyError, TypeError, NoMethodError, Errno::ENOENT => e
    raise EmberRelease::Error, "Invalid sysroot kit: #{e.message}"
  end

  def prepare(build, dir, qualification = nil)
    sha = File.read(File.join(build, 'build-success')).strip
    profile = JSON.parse(File.read(File.join(build, 'build-profile.json')))
    profile_check(profile, sha)
    source = File.join(build, 'source.tar.gz')
    check(sha256(source) == profile['source_sha256'], 'Source export differs') if File.file?(source)
    Dir.mkdir(dir, 0700)
    SETS.each { |name| FileUtils.cp(File.join(build, 'release/evbarm-aarch64/binary/sets', name), File.join(dir, name)) }
    [KERNEL, 'build-profile.json'].each { |name| FileUtils.cp(File.join(build, name), File.join(dir, name)) }
    File.write(File.join(dir, 'source-export.sha256'), "#{profile['source_sha256']}  source.tar.gz\n")
    names = REQUIRED.dup
    if File.file?(File.join(build, 'build-run.json'))
      FileUtils.cp(File.join(build, 'build-run.json'), File.join(dir, 'build-run.json'))
      names << 'build-run.json'
    end
    q = qualification ? JSON.parse(File.read(qualification)) : nil
    if q
      q = q.slice('source_commit', 'base_sha256', 'comp_sha256', 'kernel_gzip_sha256', 'consumer_compiler', 'execution', 'checks', 'runtime_libraries', 'evidence_sha256')
      q['checks'] = q.fetch('checks').slice(*CHECKS)
      File.write(File.join(dir, 'qualification.json'), JSON.pretty_generate(q) + "\n")
      names << 'qualification.json'
      FileUtils.cp(File.join(build, EVIDENCE), File.join(dir, EVIDENCE))
      names << EVIDENCE
    end
    File.write(File.join(dir, 'release-notes.md'), <<~TEXT)
      # EmberBSD 1.0 AArch64 development sysroot

      Source: `#{sha}`. NetBSD compatibility base: `11.0`; target: `evbarm/aarch64`.
      `base.tar.xz` and `comp.tar.xz` come from one clean full fork build.
      The build uses the in-tree GCC12.5 host bootstrap and base runtime.
      Native GCC12 commands are excluded. GCC16.2 application consumers are separate.
      `build-profile.json` records exact commands, effective options and compiler banners.
      `source-export.sha256` identifies the builder's git archive, not GitHub's generated archive.
      Source is public at https://github.com/oxtech-ember/EmberBSD/tree/#{sha}.
      Corresponding source archive: https://github.com/oxtech-ember/EmberBSD/archive/#{sha}.tar.gz.
      That GitHub-generated archive differs from the builder's recorded git archive.
      No upstream NetBSD release binaries or cloud bootstrap subset are substituted.

      #{q ? 'GCC16 C/C++ cross consumers and fork libc checks passed in full-system EmberBSD AArch64 QEMU TCG.' : 'Target qualification is pending. Do not publish this candidate.'}
      The matching compressed EMBER64 kernel is included for that finite test workflow.
      These are development sets, without installation-media or physical-board acceptance.
      X11, rump, compat, optional modules, manuals, debug, profile libraries and test sets are omitted.
      Keep firmware and source licence notices from the sets. Do not install these sets over a live system.
      Verify SHA256 and source provenance before extracting into an empty target sysroot.
    TEXT
    files = names.map { |name| path = File.join(dir, name); {'name' => name, 'size' => File.size(path), 'sha256' => sha256(path)} }
    manifest = {'schema' => SCHEMA, 'repository' => EmberRelease::REPOSITORY, 'source_commit' => sha,
      'release_tag' => tag(sha), 'source_archive_sha256' => profile['source_sha256'], 'architecture' => 'aarch64',
      'machine' => 'evbarm', 'product_version' => '1.0', 'base_version' => '11.0', 'build_profile' => profile,
      'files' => files, 'qualification' => q, 'hardware_qualification' => 'not-tested'}
    File.write(File.join(dir, 'manifest.json'), JSON.pretty_generate(manifest) + "\n")
    File.write(File.join(dir, 'SHA256'), EmberRelease.checksum_text(dir, names + ['manifest.json']))
    verify(dir)
  end

  class GitHub < EmberRelease::GitHub
    def verify_kit(dir)
      EmberSysroot.verify(dir)
    end
    def qualify_kit
      EmberSysroot.require_qualification(@manifest)
    end
    def title
      "EmberBSD 1.0 AArch64 #{@sha[0, 12]} development sysroot"
    end
  end
end
