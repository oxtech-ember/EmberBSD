# SPDX-License-Identifier: BSD-2-Clause
# Origin: AI-assisted EmberBSD AArch64 sysroot publication, 2026-10-16.
# Run the actual wrapper against committed host fixtures. This is not an OS build.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'
require 'zlib'

builder = File.expand_path('../release/build-aarch64.sh', __dir__)
def run!(*command)
  output, error, status = Open3.capture3(*command)
  abort "FAIL: #{command.inspect}: #{output}#{error}" unless status.success?
  output
end

Dir.mktmpdir('ember-release-aarch64-build') do |dir|
  repo = File.join(dir, 'repo')
  FileUtils.mkdir_p(File.join(repo, 'ember/release'))
  FileUtils.cp(builder, File.join(repo, 'ember/release/build-aarch64.sh'))
  %w[tools/Makefile share/mk/bsd.own.mk sys/arch/evbarm/conf/EMBER64].each do |path|
    FileUtils.mkdir_p(File.dirname(File.join(repo, path)))
    File.write(File.join(repo, path), "# Complete source sentinel.\n")
  end
  script = <<~'SH'
    #!/bin/sh
    set -eu
    previous=
    for argument do
      case "$previous" in -T) tools=$argument ;; -O) obj=$argument ;; -R) release=$argument ;; esac
      previous=$argument
    done
    out=$(dirname "$tools")
    # Reject inherited controls that could weaken diagnostics or file-list checks.
    test -z "${NOCHECKFLIST:-}${CHECKFLIST_FLAGS:-}${MAKEFLAGS:-}${MFLAGS:-}${NOGCCERROR:-}"
    printf '%s\n' "$@" >> "$out/build-arguments"
    if test "$previous" = kernel=EMBER64; then
      test ! -f fail-tools
      mkdir -p "$tools/bin" "$obj/kernels/EMBER64"
      printf '#!/bin/sh\nprintf "fixture target GCC 12.5.0\\n"\n' > "$tools/bin/aarch64--netbsd-gcc"
      chmod +x "$tools/bin/aarch64--netbsd-gcc"
      printf 'matching EMBER64 kernel\n' > "$obj/kernels/EMBER64/netbsd"
    elif test "$previous" = sets; then
      test -f "$obj/kernels/EMBER64/netbsd"
      test ! -f fail-distribution
      mkdir -p "$release/evbarm-aarch64/binary/sets"
      printf 'complete base fixture\n' > "$release/evbarm-aarch64/binary/sets/base.tar.xz"
      if test ! -f omit-comp; then
        printf 'complete comp fixture\n' > "$release/evbarm-aarch64/binary/sets/comp.tar.xz"
      fi
    else
      exit 70
    fi
  SH
  File.write(File.join(repo, 'build.sh'), script)
  run!('git', 'init', '-q', repo)
  commit = lambda do |message|
    run!('git', '-C', repo, 'add', '.')
    run!('git', '-C', repo, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
      'commit', '-qm', message)
    run!('git', '-C', repo, 'rev-parse', 'HEAD').strip
  end
  sha = commit.call('fixture')
  wrapper = File.join(repo, 'ember/release/build-aarch64.sh')
  # A dirty checkout must not supply any source to the pinned export.
  File.write(File.join(repo, 'build.sh'), "exit 99\n")
  output = File.join(dir, 'output')
  hostile = { 'NOCHECKFLIST' => 'yes', 'CHECKFLIST_FLAGS' => '-M', 'MAKEFLAGS' => 'NOGCCERROR=yes',
    'MFLAGS' => '-i', 'NOGCCERROR' => 'yes', 'EMBER_RELEASE_JOBS' => '2' }
  run!(hostile, 'sh', wrapper, sha, output)
  abort 'FAIL: uncompressed duplicate source archive' if File.exist?(File.join(output, 'source.tar'))
  abort 'FAIL: source export used a dirty file' unless File.read(File.join(output, 'source/build.sh')) == script
  abort 'FAIL: success pin differs' unless File.read(File.join(output, 'build-success')) == "#{sha}\n"
  profile = JSON.parse(File.read(File.join(output, 'build-profile.json')))
  abort 'FAIL: incomplete receipt' unless profile.values_at('schema', 'status', 'source_commit', 'machine',
    'architecture', 'release_machine_directory') == [1, 'built', sha, 'evbarm', 'aarch64', 'evbarm-aarch64']
  abort 'FAIL: source digest differs' unless profile['source_sha256'] == Digest::SHA256.file(File.join(output, 'source.tar.gz')).hexdigest
  abort 'FAIL: target compiler was guessed' unless profile['target_cc_version'] == 'fixture target GCC 12.5.0'
  abort 'FAIL: host compiler was not measured' if profile.dig('host', 'cc_version').to_s.empty?
  commands = profile.fetch('commands')
  abort 'FAIL: incorrect operation order' unless commands.length == 2 &&
    commands[0].last(2) == %w[tools kernel=EMBER64] && commands[1].last(2) == %w[distribution sets]
  observed = File.readlines(File.join(output, 'build-arguments'), chomp: true)
  actual_argv = commands.flat_map { |command| command.drop(command.index('./build.sh') + 1) }
  abort 'FAIL: receipt does not preserve actual argv' unless observed == actual_argv
  commands.each do |command|
    values = command.each_cons(2).select { |key, _| key == '-V' }.map(&:last)
    %w[USETOOLS=yes HAVE_GCC=12 TOOLCHAIN_MISSING=no MKGCC=yes MKGCCCMDS=no MKCXX=yes
       MKLIBSTDCXX=yes MKLINKLIB=yes MKSTATICLIB=yes MKPIC=yes MKPICLIB=yes MKFIRMWARE=yes
       MKX11=no MKRUMP=no MKCOMPAT=no MKDEBUG=no MKDEBUGLIB=no MKDEBUGKERNEL=no
       MKGDB=no MKBINUTILS=no MKATF=no MKKYUA=no MKMAN=no MKDOC=no MKLINT=no MKPROFILE=no
       MKREPRO=yes RELEASEMACHINEDIR=evbarm-aarch64 USE_XZ_SETS=yes].each do |value|
      abort "FAIL: missing profile value #{value}" unless values.include?(value)
    end
    ['MAKETARSETS=base comp', "EMBER_BUILD_ID=#{sha}", "BUILDINFO=EmberBSD:#{sha}",
     "KERNOBJDIR=#{output}/obj/kernels", "MKREPRO_TIMESTAMP=#{profile.fetch('source_timestamp')}"].each do |value|
      abort "FAIL: incoherent profile value #{value}" unless values.include?(value)
    end
    abort 'FAIL: foreign architecture' unless command.each_cons(2).include?(%w[-m evbarm]) &&
      command.each_cons(2).include?(%w[-a aarch64])
    abort 'FAIL: build does not sanitize inherited controls' unless command.first(2) == %w[env -i]
    abort 'FAIL: installer media selected' if (command & %w[release iso-image install-image live-image]).any?
    abort 'FAIL: disabled source checks' if values.any? { |value| value.match?(/CHECKFLIST|NOCHECK|NO.*ERROR|NOWERROR/) }
  end
  kernel = Zlib::GzipReader.open(File.join(output, 'netbsd-EMBER64.gz'), &:read)
  abort 'FAIL: copied kernel differs' unless kernel == File.read(File.join(output, 'obj/kernels/EMBER64/netbsd'))
  expected_outputs = %w[netbsd-EMBER64.gz release/evbarm-aarch64/binary/sets/base.tar.xz
    release/evbarm-aarch64/binary/sets/comp.tar.xz]
  abort 'FAIL: incomplete output hashes' unless profile.fetch('output_sha256').keys.sort == expected_outputs.sort
  profile.fetch('output_sha256').each do |path, digest|
    abort "FAIL: wrong output hash #{path}" unless digest == Digest::SHA256.file(File.join(output, path)).hexdigest
  end
  previous = File.binread(File.join(output, 'build-success'))
  _, _, result = Open3.capture3('sh', wrapper, sha, output)
  abort 'FAIL: existing output accepted' if result.success?
  abort 'FAIL: refusal changed previous accepted output' unless previous == File.binread(File.join(output, 'build-success'))

  # Exercise failed tools, failed distribution and a false successful producer.
  File.write(File.join(repo, 'build.sh'), script)
  %w[fail-tools fail-distribution omit-comp].each do |control|
    File.write(File.join(repo, control), "failure control\n")
    pin = commit.call(control)
    failure_output = File.join(dir, control)
    _, _, result = Open3.capture3('sh', wrapper, pin, failure_output)
    abort "FAIL: #{control} accepted" if result.success?
    abort "FAIL: #{control} exposed success" if File.exist?(File.join(failure_output, 'build-success'))
    receipt = JSON.parse(File.read(File.join(failure_output, 'build-profile.json')))
    abort "FAIL: #{control} has a completed receipt" unless receipt['status'] == 'started'
    FileUtils.rm(File.join(repo, control))
  end
  [['main', File.join(dir, 'bad-pin'), {}], [sha, File.join(dir, 'with space'), {}],
   [sha, File.join(repo, 'nested-output'), {}], [sha, File.join(dir, 'bad-jobs'), { 'EMBER_RELEASE_JOBS' => '00' }],
   [sha, File.join(dir, 'foreign-toolchain'), { 'EXTERNAL_TOOLCHAIN' => '/foreign' }]].each do |pin, destination, environment|
    _, _, result = Open3.capture3(environment, 'sh', wrapper, pin, destination)
    abort "FAIL: invalid request accepted: #{destination}" if result.success?
    abort "FAIL: invalid request created output: #{destination}" if File.exist?(destination)
  end
  if ARGV[0]
    # Evaluate the real source defaults with bmake; orchestration alone cannot
    # establish that a make option has the intended semantics.
    variables = commands[0].each_cons(2).select { |key, _| key == '-V' }.map(&:last)
    source = File.expand_path(ARGV[1] || '../..', __dir__)
    makefile = File.join(dir, 'profile.mk')
    File.write(makefile, "MACHINE=evbarm\nMACHINE_ARCH=aarch64\nNETBSDSRCDIR=#{source}\n.include \"#{source}/share/mk/bsd.own.mk\"\n")
    actual = run!(ARGV[0], '-m', File.join(source, 'share/mk'), '-f', makefile, *variables,
      '-V', '${MKGCC} ${MKGCCCMDS} ${MKCXX} ${MKLIBSTDCXX} ${MKCOMPAT} ${MKFIRMWARE}')
    abort "FAIL: real make ignores runtime profile: #{actual}" unless actual.strip == 'yes no yes yes no yes'
  end
end
puts 'PASS: pinned AArch64 export, complete base/comp orchestration, measured toolchain, hashes and failure isolation'
