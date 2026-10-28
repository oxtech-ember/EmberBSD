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
  src, tools, compiler, libedit = %w[src tools compiler libedit].map { |n| File.join(work, n) }
  [src, tools + '/bin', compiler + '/bin', libedit].each { |p| FileUtils.mkdir_p(p) }
  File.write(libedit + '/libedit-source.json', "{}\n")
  File.write(libedit + '/ports.distinfo', "synthetic\n")
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
    if [ -n "$TEST_LATE_PROFILE_FILE" ]; then printf '\n# late fixture change\n' >> "$TEST_LATE_PROFILE_FILE"; fi
    if [ -n "$TEST_LATE_DATA_FILE" ]; then printf 'late change\n' >> "$TEST_LATE_DATA_FILE"; fi
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
  File.write(compiler + '/bin/clang', "#!/bin/sh\nexit 0\n")
  FileUtils.cp(compiler + '/bin/aarch64--netbsd-readelf', compiler + '/bin/llvm-readelf')
  %w[libarchive-source libevent-source byacc-source].each do |name|
    FileUtils.mkdir_p(work + '/' + name)
    File.write(work + '/' + name + '/emberbsd-prepared-source.json', name)
  end
  FileUtils.mkdir_p(work + '/llvm-source')
  File.write(work + '/llvm-source/llvm-source.json', 'prepared LLVM source')
  FileUtils.mkdir_p(work + '/llvm-resource/lib/netbsd')
  File.write(work + '/llvm-resource/lib/netbsd/builtins.a', 'retained LLVM builtins')
  # A native-mode TOOLDIR need not contain nbmake; make comes from the routing.
  %w[nbmkesdb nbmkcsmapper].each { |tool| File.write(tools + '/bin/' + tool, "#!/bin/sh\nexit 0\n") }
  File.write(src + '/build.sh', <<~'SH')
    #!/bin/sh
    TEST_BUILD_ARGS="$*"
    while [ "$#" -gt 0 ]; do
      case "$1" in -D) shift; dest=$1;; -w) shift; wrapper=$1;; esac
      shift
    done
    export dest
    printf '%s\n' "$TEST_BUILD_ARGS" > "$dest/../build-flags"
    printf '%s\n' "$dest" > "$wrapper.dest"
    cat > "$wrapper" <<'WRAPPER'
    #!/bin/sh
    dest=$(cat "$0.dest")
    printf '%s\n' "$*" >> "$dest/../events"
    printf '%s\n' "$CPPFLAGS" > "$dest/../header-flags"
    case "$*" in *"/sbin/mknod "*"$TEST_FAIL_MKNOD_STAGE")
      test -z "$TEST_FAIL_MKNOD_STAGE" || exit 17;; esac
    case "$*" in *"$TEST_FAIL_COMPONENT"*)
      test -z "$TEST_FAIL_COMPONENT" || exit 17;; esac
    case "$*" in *'/share/i18n/esdb '*)
      selected=0
      for arg do [ "$arg" != 'SUBDIR=ISO646 UTF' ] || selected=1; done
      [ "$selected" = 1 ] || exit 71;;
    esac
    case "$*" in *'/share/i18n/'*' install')
      files=
      case "$*" in
      *'/iconv '*) files='iconv/iconv.dir';;
      *'/esdb '*) files='esdb/esdb.dir esdb/esdb.alias esdb/esdb.dir.db esdb/esdb.alias.db esdb/ISO646/ISO646-US.esdb esdb/UTF/UTF-8.esdb';;
      *'/csmapper '*) files='csmapper/mapper.dir csmapper/mapper.dir.db csmapper/charset.pivot csmapper/charset.pivot.pvdb csmapper/ISO646/ISO646-US%UCS.646';;
      esac
      for file in $files; do
        test "$file" != "$TEST_OMIT_DATA" || continue
        rel=usr/share/i18n/$file
        mkdir -p "$dest/$(dirname "$rel")"
        dir=$(dirname "$rel")
        while [ "$dir" != . ]; do
          printf './%s type=dir uname=root gname=wheel mode=0755\n' "$dir" >> "$dest/METALOG"
          dir=$(dirname "$dir")
        done
        printf 'synthetic native recipe output\n' > "$dest/$rel"
        printf './%s type=file uname=root gname=wheel mode=0444 size=%s\n' \
          "$rel" "$(wc -c < "$dest/$rel" | tr -d ' ')" >> "$dest/METALOG"
      done;;
    esac
    case "$*" in *'/share/i18n/csmapper '*' install')
      case "$TEST_BAD_DATA" in
      metadata) printf './usr/share/i18n/iconv/iconv.dir type=file uname=root gname=wheel mode=0444 content=/etc/passwd\n' >> "$dest/METALOG";;
      path) printf './usr/share/i18n/../escape type=file uname=root gname=wheel mode=0444\n' >> "$dest/METALOG";;
      unsupported) printf 'unsupported\n' > "$dest/usr/share/i18n/esdb/unreviewed.esdb";;
      esac;;
    esac
    case "$*" in
      *do-distrib-dirs*) mkdir -p "$dest/bin" "$dest/sbin" "$dest/usr/bin" \
        "$dest/usr/sbin" "$dest/usr/libexec";;
      *"/sbin/mknod install")
        if [ "$TEST_OMIT_BINARY" != sbin/mknod ]; then
          printf 'candidate\n' > "$dest/sbin/mknod"
        fi;;
      *install*)
        for file in bin/sh bin/stty usr/sbin/usbdevs sbin/init sbin/ifconfig sbin/dhcpcd sbin/fsck \
          sbin/fsck_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl \
          usr/bin/stat usr/bin/wall usr/sbin/wpa_supplicant usr/sbin/wpa_cli \
          usr/sbin/powerd usr/libexec/getty sbin/mount_ptyfs usr/sbin/sshd \
          usr/libexec/sshd-session usr/libexec/sshd-auth usr/bin/ssh-keygen \
          usr/bin/passwd usr/sbin/pwd_mkdb usr/sbin/envstat sbin/nvmectl sbin/wdogctl \
          usr/sbin/acpidump; do
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
    system('/usr/bin/git', '-C', src, *args, exception: true)
  end
  native_prefix = ENV.fetch('EMBER_TEST_NATIVE_PREFIX')
  native_sh = native_prefix + '/bin/mksh'
  routing = work + '/routing'
  FileUtils.mkdir_p(routing)
  File.symlink(native_prefix + '/bin/nawk', routing + '/awk')
  role_log = work + '/role-calls'
  routes = {}
  {'git' => '/usr/bin/git', 'ruby' => native_prefix + '/bin/ruby',
   'shasum' => native_prefix + '/bin/shasum'}.each do |role, real|
    route = work + '/selected-' + role
    File.write(route, "#!#{native_sh}\nprintf '#{role}\n' >> '#{role_log}'\nexec '#{real}' \"$@\"\n")
    File.chmod(0o755, route)
    routes['TEST_' + role.upcase] = route
  end
  sdk = work + '/sdk'
  FileUtils.mkdir_p(sdk + '/scripts')
  File.write(sdk + '/scripts/run-native-os.py', <<~'PYTHON')
    import os, subprocess, sys
    args = sys.argv[1:]
    source = args[args.index('--source') + 1]
    build_args = args[args.index('--') + 1:]
    sys.exit(subprocess.call([os.environ['HOST_SH'], source + '/build.sh', *build_args]))
  PYTHON
  selected_runner = lambda do |script, out, extra|
    Open3.capture3({'HOST_SH' => native_sh, 'PATH' => routing + ':' + native_prefix + '/bin:' + native_prefix + '/gnu/bin'}.merge(extra),
      native_sh, script, '--selected', src, out, tools, compiler, sdk, native_prefix,
      'clang', extra.fetch('TEST_FAMILY', 'gcc'), extra.fetch('EMBER_LIBEDIT_SOURCE', libedit),
      work + '/llvm-source', work + '/host-bfd.mk', '2',
      extra.fetch('TEST_GIT', '/usr/bin/git'), extra.fetch('TEST_RUBY', native_prefix + '/bin/ruby'),
      extra.fetch('TEST_SHASUM', native_prefix + '/bin/shasum'),
      work + '/libarchive-source', work + '/libevent-source', work + '/byacc-source', work + '/llvm-runtime',
      work + '/llvm-resource', compiler + '/libgcc_eh.a', compiler + '/resource',
      '23.1.4', 'aarch64-unknown-netbsd', work + '/llvm-resource/lib/netbsd/builtins.a')
  end
  run = lambda do |out, env = {}|
    selected_runner.call(builder, out, {'EMBER_LIBEDIT_SOURCE' => libedit, 'TEST_FAIL_COMPONENT' => '', 'TEST_OMIT_BINARY' => '',
      'TEST_ELF_KIND' => '', 'TEST_FAIL_MKNOD_STAGE' => '', 'TEST_OMIT_DATA' => '',
      'TEST_BAD_DATA' => '', 'TEST_LATE_PROFILE_FILE' => '', 'TEST_LATE_DATA_FILE' => ''}.merge(routes).merge(env))
  end
  out = work + '/accepted'
  stdout, stderr, status = run.call(out)
  abort "FAIL: valid profile: #{stdout}#{stderr}" unless status.success?
  clang_out = work + '/clang-accepted'
  stdout, stderr, status = run.call(clang_out, 'TEST_FAMILY' => 'clang')
  abort "FAIL: Clang profile: #{stdout}#{stderr}" unless status.success?
  clang_flags = File.read(clang_out + '/build-flags')
  %w[EMBER_COMPILER_FAMILY=clang EMBER_LLVM_RUNTIME_PHASE=bootstrap MKLIBCXX=no MKCOMPAT=no].each do |flag|
    abort "FAIL: missing Clang flag #{flag}" unless clang_flags.include?(flag)
  end
  %w[LIBARCHIVE LIBEVENT BYACC].each do |name|
    abort "FAIL: #{name} source not forwarded" unless clang_flags.include?("EMBER_#{name}_SOURCE=#{work}/#{name.downcase}-source")
  end
  abort 'FAIL: selected resource not forwarded' unless
    clang_flags.include?("EMBER_LLVM_RESOURCE_DIR=#{work}/llvm-resource")
  checks += 1
  events = File.readlines(out + '/events')
  dhcpcd = events.select { |line| line.include?('/external/bsd/dhcpcd/sbin/dhcpcd ') }
  abort 'FAIL: reduced DHCP must retain IPv4 and enable IPv6 in every build stage' unless
    dhcpcd.size == 3 && dhcpcd.map { |line| line.split.last } == %w[obj dependall install] &&
    dhcpcd.all? { |line| line.include?(' SMALLPROG=1 SMALLPROG_INET6=1 ') }
  checks += 1
  { 'iconv' => '', 'esdb' => 'SUBDIR=ISO646 UTF ISO646_PART=US UTF_PART=8 ',
    'csmapper' => 'SUBDIR=ISO646 PART_ISO646=US ' }.each do |part, selection|
    selected = events.select { |line| line.include?("/share/i18n/#{part} ") }
    abort "FAIL: native DATA component/selection #{part}" unless selected.size == 3 &&
      selected[0].end_with?(selection + "obj\n") && selected[1].end_with?(selection + "dependall\n") &&
      selected[2].end_with?(selection + "install\n")
    checks += 1
  end
# Catch omitted SSH/account components and implicit auth-module dependencies.
ssh_parts = %w[external/bsd/blocklist/lib lib/libwrap crypto/external/bsd/openssh/lib
  crypto/external/bsd/openssh/bin/sshd crypto/external/bsd/openssh/libexec/sshd-session
  crypto/external/bsd/openssh/libexec/sshd-auth crypto/external/bsd/openssh/bin/ssh-keygen
  usr.bin/passwd usr.sbin/pwd_mkdb sbin/mount_ptyfs]
ssh_parts.each do |part|
  selected = events.select { |line| line.include?("/#{part} ") }
  wanted = part == 'crypto/external/bsd/openssh/lib' ? %w[obj dependall] : %w[obj dependall install]
  abort "FAIL: incomplete native SSH dependency #{part}" unless selected.map { |l| l.split.last } == wanted
  checks += 1
end
abort 'FAIL: prepared libedit source not passed to the build' unless
  File.read(out + '/build-flags').include?("EMBER_LIBEDIT_SOURCE=#{libedit} ")
checks += 1
abort 'FAIL: prepared libedit receipt not recorded' unless
  File.read(out + '/BUILD-INPUTS.txt').include?("#{libedit}/libedit-source.json")
checks += 1
abort 'FAIL: prepared byacc source or receipt missing' unless
  File.read(out + '/build-flags').include?("EMBER_BYACC_SOURCE=#{work}/byacc-source") &&
  File.read(out + '/BUILD-INPUTS.txt').include?("#{work}/byacc-source/emberbsd-prepared-source.json")
checks += 1
%w[PAM KERBEROS LDAP YP SKEY HESIOD].each do |feature|
  flags = File.read(out + '/build-flags')
  abort "FAIL: optional auth feature enabled #{feature}" unless
    flags.include?("MK#{feature}=no") && flags.include?("USE_#{feature}=no")
  checks += 1
end
dependency_order = %w[external/bsd/blocklist/lib lib/libwrap crypto/external/bsd/openssh/lib
  crypto/external/bsd/openssh/bin/sshd]
positions = dependency_order.map { |part| events.index { |line| line.include?("/#{part} ") } }
abort 'FAIL: SSH library dependency order' unless positions == positions.sort && positions.uniq.size == 4
checks += 1
  inventory = out + '/I18N-INVENTORY.json'
  abort 'FAIL: separate DATA inventory absent' unless File.file?(inventory)
  checks += 1
  %w[bin/stty usr.sbin/usbdevs sbin/mknod usr.sbin/envstat sbin/nvmectl sbin/wdogctl
     usr.sbin/acpitools/acpidump].each do |part|
    selected = events.select { |line| line.include?("/#{part} ") }
    abort "FAIL: incomplete diagnostic component #{part}" unless
      selected.size == 3 && selected[0].end_with?(" obj\n") &&
      selected[1].end_with?(" dependall\n") && selected[2].end_with?(" install\n")
    checks += 1
  end
  abort 'FAIL: envstat before its static libmj dependency' unless
    events.index { |line| line.include?('/crypto/external/bsd/netpgp/libmj ') && line.end_with?(" install\n") } <
    events.index { |line| line.include?('/usr.sbin/envstat ') }
  checks += 1
  abort 'FAIL: mknod before its static libc dependency' unless
    events.index { |line| line.include?('/lib/libc ') && line.end_with?(" install\n") } <
    events.index { |line| line.include?('/sbin/mknod ') }
  checks += 1
  mknod = out + '/dest/sbin/mknod'
  abort 'FAIL: installed mknod absent from the executable receipt' unless File.file?(mknod) &&
    File.readlines(out + '/BINARIES-SHA256.txt').include?("#{Digest::SHA256.file(mknod).hexdigest}  #{mknod}\n")
  checks += 1
  %w[lib/libc external/bsd/wpa/bin/wpa_supplicant
     external/bsd/dhcpcd/sbin/dhcpcd sbin/fsck_ffs sbin/mount_msdos
     sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl
     usr.bin/stat usr.bin/wall libexec/getty usr.sbin/powerd
     crypto/external/bsd/netpgp/libmj].each do |part|
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
  abort 'FAIL: DATA inventory not pinned in accepted receipt' unless
    receipt.include?('i18n-inventory-sha256=' + Digest::SHA256.file(inventory).hexdigest)
  checks += 1
  %w[nbmkesdb nbmkcsmapper].each do |tool|
    abort "FAIL: no native generator hash #{tool}" unless receipt.include?(Digest::SHA256.file(tools + '/bin/' + tool).hexdigest)
    checks += 1
  end
  abort 'FAIL: no compiler runtime hash' unless receipt.include?(Digest::SHA256.file(compiler + '/libgcc.a').hexdigest)
  checks += 1
  abort 'FAIL: no compiler unwinder hash' unless receipt.include?(Digest::SHA256.file(compiler + '/libgcc_eh.a').hexdigest)
  checks += 1
  abort 'FAIL: no LLVM source hash' unless receipt.include?(Digest::SHA256.file(work + '/llvm-source/llvm-source.json').hexdigest)
  checks += 1
  abort 'FAIL: selected native role was bypassed' unless %w[git ruby shasum].all? { |role| File.readlines(role_log).any? { |line| line.strip == role } }
  checks += 1
  if ENV['EMBER_TEST_SMOKE'] == 'yes'
    puts "CIX reduced userland smoke: #{checks} assertions, 0 failures"
    exit 0
  end
  _, _, status = run.call(out)
  abort 'FAIL: replaced existing output' if status.success?
  checks += 1
  _, stderr, status = run.call(work + '/no-libedit', 'EMBER_LIBEDIT_SOURCE' => '')
  abort 'FAIL: accepted build without prepared libedit source' if status.success? ||
    !stderr.include?('EMBER_LIBEDIT_SOURCE') || File.exist?(work + '/no-libedit')
  checks += 1
  File.unlink(libedit + '/libedit-source.json')
  _, _, status = run.call(work + '/partial-libedit')
  File.write(libedit + '/libedit-source.json', "{}\n")
  abort 'FAIL: accepted incomplete prepared libedit source' if status.success? || File.exist?(work + '/partial-libedit')
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
%w[external/bsd/blocklist/lib lib/libwrap crypto/external/bsd/openssh/lib
   crypto/external/bsd/openssh/libexec/sshd-auth].each_with_index do |part, index|
  output = work + '/failed-ssh-' + index.to_s
  _, _, status = run.call(output, 'TEST_FAIL_COMPONENT' => "/#{part} ")
  abort "FAIL: continued failed SSH dependency #{part}" if status.success? || File.exist?(output + '/BUILD-INPUTS.txt')
  checks += 1
end
%w[sbin/mount_ptyfs usr/sbin/sshd usr/libexec/sshd-session usr/libexec/sshd-auth
   usr/bin/ssh-keygen usr/bin/passwd usr/sbin/pwd_mkdb].each_with_index do |binary, index|
  output = work + '/missing-ssh-' + index.to_s
  _, stderr, status = run.call(output, 'TEST_OMIT_BINARY' => binary)
  abort "FAIL: accepted absent required SSH/account program #{binary}" if status.success? ||
    !stderr.include?('Missing required program: ' + binary) || File.exist?(output + '/BUILD-INPUTS.txt')
  checks += 1
end
  _, _, status = run.call(work + '/missing-fsck', 'TEST_OMIT_BINARY' => 'sbin/fsck_ffs')
  abort 'FAIL: accepted cohort without fsck_ffs' if status.success? || File.exist?(work + '/missing-fsck/BUILD-INPUTS.txt')
  checks += 1
  _, _, status = run.call(work + '/missing-getty', 'TEST_OMIT_BINARY' => 'usr/libexec/getty')
  abort 'FAIL: accepted cohort without getty' if status.success? || File.exist?(work + '/missing-getty/BUILD-INPUTS.txt')
  checks += 1
  _, _, status = run.call(work + '/missing-powerd', 'TEST_OMIT_BINARY' => 'usr/sbin/powerd')
  abort 'FAIL: accepted cohort without powerd' if status.success? || File.exist?(work + '/missing-powerd/BUILD-INPUTS.txt')
  checks += 1
  %w[bin/stty usr.sbin/usbdevs crypto/external/bsd/netpgp/libmj usr.sbin/envstat sbin/nvmectl
     sbin/wdogctl usr.sbin/acpitools/acpidump].each_with_index do |part, index|
    output = work + '/failed-diagnostic-' + index.to_s
    _, _, status = run.call(output, 'TEST_FAIL_COMPONENT' => "/#{part} ")
    abort "FAIL: accepted failed diagnostic component #{part}" if status.success? ||
      File.exist?(output + '/BUILD-INPUTS.txt')
    checks += 1
  end
  %w[bin/stty usr/sbin/usbdevs usr/sbin/envstat sbin/nvmectl sbin/wdogctl
     usr/sbin/acpidump].each_with_index do |binary, index|
    output = work + '/missing-diagnostic-' + index.to_s
    _, _, status = run.call(output, 'TEST_OMIT_BINARY' => binary)
    abort "FAIL: accepted cohort without #{binary}" if status.success? ||
      File.exist?(output + '/BUILD-INPUTS.txt')
    checks += 1
  end
  %w[obj dependall install].each do |stage|
    output = work + '/failed-mknod-' + stage
    _, _, status = run.call(output, 'TEST_FAIL_MKNOD_STAGE' => stage)
    abort "FAIL: accepted failed mknod #{stage}" if status.success? ||
      File.exist?(output + '/BUILD-INPUTS.txt') || File.exist?(output + '/BINARIES-SHA256.txt')
    checks += 1
    failed_events = File.readlines(output + '/events')
    abort "FAIL: continued after failed mknod #{stage}" unless
      failed_events.last.include?('/sbin/mknod ') && failed_events.last.end_with?(" #{stage}\n") &&
      failed_events.none? { |line| line.include?('/sbin/ifconfig ') }
    checks += 1
  end
  output = work + '/missing-mknod'
  _, stderr, status = run.call(output, 'TEST_OMIT_BINARY' => 'sbin/mknod')
  abort 'FAIL: accepted cohort without native mknod' if status.success? ||
    !stderr.include?('Missing required program: sbin/mknod') || File.exist?(output + '/BUILD-INPUTS.txt')
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
  %w[iconv esdb csmapper].each do |part|
    output = work + '/failed-data-' + part
    _, _, status = run.call(output, 'TEST_FAIL_COMPONENT' => "/share/i18n/#{part} ")
    abort "FAIL: accepted failed native DATA #{part}" if status.success? || File.exist?(output + '/BUILD-INPUTS.txt')
    checks += 1
  end
  %w[iconv/iconv.dir esdb/esdb.dir esdb/esdb.alias esdb/ISO646/ISO646-US.esdb esdb/UTF/UTF-8.esdb csmapper/mapper.dir csmapper/ISO646/ISO646-US%UCS.646].each_with_index do |path, i|
    output = work + '/missing-data-' + i.to_s
    _, _, status = run.call(output, 'TEST_OMIT_DATA' => path)
    abort "FAIL: accepted missing DATA #{path}" if status.success? || File.exist?(output + '/BUILD-INPUTS.txt')
    checks += 1
  end
  %w[metadata path unsupported].each do |kind|
    output = work + '/unsafe-data-' + kind
    _, _, status = run.call(output, 'TEST_BAD_DATA' => kind)
    abort "FAIL: accepted unsafe native DATA #{kind}" if status.success? || File.exist?(output + '/BUILD-INPUTS.txt')
    checks += 1
  end
  output = work + '/late-data'
  _, _, status = run.call(output, 'TEST_LATE_DATA_FILE' => output + '/dest/usr/share/i18n/iconv/iconv.dir')
  abort 'FAIL: accepted DATA mutated after inventory capture' if status.success? || File.exist?(output + '/BUILD-INPUTS.txt')
  checks += 1
  profile = work + '/profile'
  FileUtils.mkdir_p(profile)
  %w[cix-p1-build-userland.sh cix-p1-native.py cix-p1-i18n.rb].each { |p| FileUtils.cp(File.join(__dir__, p), profile + '/' + p) }
  profile_output = work + '/late-profile'
  _, _, status = selected_runner.call(profile + '/cix-p1-build-userland.sh', profile_output,
    { 'TEST_LATE_PROFILE_FILE' => profile + '/cix-p1-i18n.rb' })
  abort 'FAIL: accepted helper changed during build' if status.success? || File.exist?(profile_output + '/BUILD-INPUTS.txt')
  checks += 1
  File.write(src + '/untracked.h', 'unrecorded source input')
  _, _, status = run.call(work + '/untracked-source')
  abort 'FAIL: accepted untracked source input' if status.success? || File.exist?(work + '/untracked-source')
  checks += 1
end
puts "CIX reduced userland: #{checks} assertions, 0 failures"
