#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Original EmberBSD reduced-userland orchestration checks, AI-assisted.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'digest'

builder = File.expand_path('cix-p1-build-userland.sh', __dir__)
abort 'FAIL: reduced userland builder is missing' unless File.file?(builder)
checks = 0
Dir.mktmpdir('ember-cix-userland-') do |work|
  src, tools, compiler = %w[src tools compiler].map { |n| File.join(work, n) }
  [src, tools + '/bin', compiler + '/bin'].each { |p| FileUtils.mkdir_p(p) }
  File.write(compiler + '/libgcc.a', 'retained compiler runtime')
  File.write(compiler + '/libgcc_eh.a', 'retained compiler unwinder')
  File.write(compiler + '/bin/aarch64--netbsd-gcc', <<~SH)
    #!/bin/sh
    case "$1" in
    -dumpfullversion) echo "${TEST_GCC_VERSION:-16.2.0}";;
    -dumpmachine) echo aarch64--netbsd;;
    -print-libgcc-file-name) echo '#{compiler}/libgcc.a';;
    -print-file-name=libgcc_eh.a)
      if [ "$TEST_NO_UNWINDER" = 1 ]; then echo libgcc_eh.a; else echo '#{compiler}/libgcc_eh.a'; fi;;
    -print-file-name=include) echo '#{compiler}/include';;
    -print-file-name=include-fixed) echo '#{compiler}/include-fixed';;
    *) exit 2;;
    esac
  SH
  File.write(compiler + '/bin/aarch64--netbsd-readelf', <<~SH)
    #!/bin/sh
    test -s "$2" || exit 1
    test "$TEST_ELF_KIND" != invalid || exit 1
    echo 'Class: ELF64'
    if [ "$TEST_ELF_KIND" = big-endian ]; then
      echo "Data: 2's complement, big endian"
    else
      echo "Data: 2's complement, little endian"
    fi
    echo 'Machine: AArch64'
    echo 'Type: EXEC (Executable file)'
    if [ "$TEST_ELF_KIND" = dynamic ] && [ "$1" = -l ]; then echo INTERP; fi
  SH
  File.write(tools + '/bin/nbmake', "#!/bin/sh\nexit 0\n")
  File.write(src + '/build.sh', <<~'SH')
    #!/bin/sh
    while [ "$#" -gt 0 ]; do
      case "$1" in -D) shift; dest=$1;; -w) shift; wrapper=$1;; esac
      shift
    done
    export dest
    printf '%s\n' "$dest" > "$wrapper.dest"
    cat > "$wrapper" <<'WRAPPER'
    #!/bin/sh
    dest=$(cat "$0.dest")
    printf '%s\n' "$*" >> "$dest/../events"
    printf '%s\n' "$CPPFLAGS" > "$dest/../header-flags"
    case "$*" in *"$TEST_FAIL_COMPONENT"*)
      test -z "$TEST_FAIL_COMPONENT" || exit 17;; esac
    case "$*" in
      *do-distrib-dirs*) mkdir -p "$dest/bin" "$dest/sbin" "$dest/usr/bin" \
        "$dest/usr/sbin" "$dest/usr/libexec";;
      *install*)
        for file in bin/sh sbin/init sbin/ifconfig sbin/dhcpcd sbin/fsck \
          sbin/fsck_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl \
          usr/bin/stat usr/bin/wall usr/sbin/wpa_supplicant usr/sbin/wpa_cli usr/libexec/getty; do
          test "$file" = "$TEST_OMIT_BINARY" && continue
          mkdir -p "$dest/$(dirname "$file")"
          printf 'candidate\n' > "$dest/$file"
        done;;
    esac
    WRAPPER
    chmod 755 "$wrapper"
  SH
  Dir[compiler + '/bin/*', tools + '/bin/*'].each { |p| File.chmod(0o755, p) }
  %w[init add commit].each do |operation|
    args = case operation
    when 'init' then %w[init -q]
    when 'add' then %w[add build.sh]
    else ['-c', 'user.name=Contract', '-c', 'user.email=contract@example.invalid',
          'commit', '-qm', 'fixture']
    end
    system('git', '-C', src, *args, exception: true)
  end
  run = lambda do |out, env = {}|
    Open3.capture3({'TEST_FAIL_COMPONENT' => '', 'TEST_OMIT_BINARY' => '',
      'TEST_ELF_KIND' => ''}.merge(env),
      'sh', builder, src, out, tools, compiler)
  end
  out = work + '/accepted'
  stdout, stderr, status = run.call(out)
  abort "FAIL: valid profile: #{stdout}#{stderr}" unless status.success?
  events = File.readlines(out + '/events')
  %w[lib/libc external/bsd/wpa/bin/wpa_supplicant
     external/bsd/dhcpcd/sbin/dhcpcd sbin/fsck_ffs sbin/mount_msdos
     sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl
     usr.bin/stat usr.bin/wall libexec/getty].each do |part|
    abort "FAIL: missing component #{part}" unless events.any? { |l| l.include?("/#{part} ") }
    checks += 1
  end
  abort 'FAIL: headers after libc' unless events.index { |e| e.include?('includes') } <
    events.index { |e| e.include?('/lib/libc ') }
  checks += 1
  flags = File.read(out + '/header-flags')
  abort 'FAIL: compiler can fall back to an old target header tree' unless
    flags.include?('-nostdinc') && flags.include?(out + '/dest/usr/include')
  checks += 1
  receipt = File.read(out + '/BUILD-INPUTS.txt')
  abort 'FAIL: no compiler runtime hash' unless receipt.include?(Digest::SHA256.file(compiler + '/libgcc.a').hexdigest)
  checks += 1
  abort 'FAIL: no compiler unwinder hash' unless receipt.include?(Digest::SHA256.file(compiler + '/libgcc_eh.a').hexdigest)
  checks += 1
  _, _, status = run.call(out)
  abort 'FAIL: replaced existing output' if status.success?
  checks += 1
  _, _, status = run.call(work + '/old-gcc', 'TEST_GCC_VERSION' => '15.2.0')
  abort 'FAIL: accepted non-GCC16 compiler' if status.success? || File.exist?(work + '/old-gcc')
  checks += 1
  _, _, status = run.call(work + '/failed', 'TEST_FAIL_COMPONENT' => '/lib/libc ')
  abort 'FAIL: continued failed libc build' if status.success? || File.exist?(work + '/failed/BUILD-INPUTS.txt')
  checks += 1
  failed_events = File.read(work + '/failed/events')
  abort 'FAIL: built programs after failed libc' if failed_events.include?('/sbin/ifconfig ')
  checks += 1
  _, _, status = run.call(work + '/missing-fsck', 'TEST_OMIT_BINARY' => 'sbin/fsck_ffs')
  abort 'FAIL: accepted cohort without fsck_ffs' if status.success? || File.exist?(work + '/missing-fsck/BUILD-INPUTS.txt')
  checks += 1
  _, _, status = run.call(work + '/missing-getty', 'TEST_OMIT_BINARY' => 'usr/libexec/getty')
  abort 'FAIL: accepted cohort without getty' if status.success? || File.exist?(work + '/missing-getty/BUILD-INPUTS.txt')
  checks += 1
  _, _, status = run.call(work + '/c-runtime', 'TEST_NO_UNWINDER' => '1')
  abort 'FAIL: C-only compiler without unwinder rejected' unless status.success? &&
    File.read(work + '/c-runtime/BUILD-INPUTS.txt').include?('compiler-unwinder=not-provided')
  checks += 1
  %w[invalid dynamic big-endian].each do |kind|
    _, _, status = run.call(work + '/' + kind, 'TEST_ELF_KIND' => kind)
    abort "FAIL: accepted #{kind} executable" if status.success? || File.exist?(work + '/' + kind + '/BUILD-INPUTS.txt')
    checks += 1
  end
  File.write(src + '/untracked.h', 'unrecorded source input')
  _, _, status = run.call(work + '/untracked-source')
  abort 'FAIL: accepted untracked source input' if status.success? || File.exist?(work + '/untracked-source')
  checks += 1
end
puts "CIX reduced userland: #{checks} assertions, 0 failures"
