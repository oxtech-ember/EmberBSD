#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted actual-script host controls.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'

root = File.expand_path('../..', __dir__)
builder = ENV.fetch('FIRMWARE_BUILDER', "#{root}/ember/build-firmware-selected.sh")
python = ENV.fetch('FIRMWARE_TEST_PYTHON') do
  ENV.fetch('PATH').split(File::PATH_SEPARATOR).map { |path| "#{path}/python3" }
    .find { |path| File.file?(path) && File.executable?(path) }
end
abort 'A host Python 3 is needed to execute the existing receipt writer' unless python && File.executable?(python)
checks = 0
failures = []
variants = %w[firmware firmware-rp1-console firmware-rp1-trace]
evidence = ENV['FIRMWARE_CONTRACT_EVIDENCE']
FileUtils.mkdir_p(evidence) if evidence

Dir.mktmpdir('firmware-contract-', ENV['TMPDIR']) do |temporary|
  test = lambda do |name, arguments, values = {}, expected = 1, selected = 'firmware', preserve = false|
    next if ENV['FIRMWARE_CONTRACT_FILTER'] && !name.include?(ENV['FIRMWARE_CONTRACT_FILTER'])
    directory = "#{temporary}/#{name}"
    tools = "#{directory}/bin"
    cache = "#{directory}/cache"
    FileUtils.mkdir_p(tools)
    variants.each do |variant|
      FileUtils.mkdir_p("#{cache}/#{variant}")
      %w[RPI_EFI.fd RPI_EFI.fd.incoming firmware-receipt.json].each do |file|
        File.write("#{cache}/#{variant}/#{file}", "accepted #{variant}/#{file}\n")
      end
    end
    executable = lambda do |file, body|
      File.write("#{tools}/#{file}", body)
      File.chmod(0o700, "#{tools}/#{file}")
    end
    executable.call('uname', "#!/bin/sh\n[ \"$1\" = -s ] && printf '%s\\n' \"$FIXTURE_HOST\" || echo arm64\n")
    %w[gmake iasl dtc aarch64-none-elf-gcc-ar explicit-gcc-ar aarch64-none-elf-as aarch64-none-elf-ld aarch64-none-elf-objcopy aarch64-none-elf-objdump explicit-as explicit-ld explicit-objcopy explicit-objdump].each { |tool| executable.call(tool, "#!/bin/sh\nexit 0\n") }
    fake_build = <<~RUBY
      #!#{RbConfig.ruby}
      require 'json'
      search = %w[CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH COMPILER_PATH GCC_EXEC_PREFIX LD_RUN_PATH LD_LIBRARY_PATH CFLAGS CPPFLAGS CXXFLAGS ASFLAGS LDFLAGS LDLIBS CC CXX CPP AS LD AR NM OBJCOPY OBJDUMP RANLIB STRIP MAKEFLAGS MFLAGS MAKEOVERRIDES]
      abort 'Inherited compiler search environment reached the build' if search.any? { |key| ENV.key?(key) }
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['build', ARGV]) }
      lock=File.dirname(Dir.pwd)+'/.ember-firmware.lock'
      abort 'Upstream build escaped the cache lease' unless File.directory?(lock)
      exit 43 if ENV['FIXTURE_FAIL'] == 'build'
      if ENV['FIXTURE_FAIL']=='manifest-changed'
        File.write(File.dirname(Dir.pwd)+'/.ember-firmware-source.json','changed during build')
      end
      File.write('RPI_EFI.fd', "new fixture firmware\n")
      if ENV['FIXTURE_FAIL'] == 'fill-root'
        directory = ARGV.join(' ').match(/EMBER_FIRMWARE_ROOT=([^ ]+)/)[1]
        File.write(directory + '/unexpected', 'fixture input')
      end
    RUBY
    executable.call('git', <<~RUBY)
      #!#{RbConfig.ruby}
      require 'fileutils'
      require 'json'
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['git', ARGV]) }
      exit 41 if ENV['FIXTURE_FAIL'] == 'git' && (ARGV.include?('fetch') || ARGV.first=='clone')
      if ARGV.first == 'clone'
        destination = ARGV.last
        FileUtils.mkdir_p(%w[.git edk2 edk2-platforms arm-trusted-firmware].map { |p| destination + '/' + p })
        File.write(destination + '/build.sh', #{fake_build.dump})
        File.chmod(0o700, destination + '/build.sh')
      end
    RUBY
    executable.call('patch', <<~RUBY)
      #!#{RbConfig.ruby}
      require 'json'
      abort 'Patch requires noninteractive forward flags' unless ARGV[0,5]==%w[-s -t -N -p1 -F0]
      input=STDIN.read
      tfa=input.include?('--- a/make_helpers/utilities.mk')
      directory=ARGV.fetch(ARGV.index('-d')+1)
      File.open(ENV.fetch('FIXTURE_TRACE'),'a') { |f| f.puts JSON.generate(['fake-patch',ARGV,tfa]) }
      edk2=input.include?('--- a/BaseTools/') || input.include?('--- a/MdeModulePkg/') || input.include?('--- a/EmbeddedPkg/')
      top=input.include?('--- a/build.sh')
      expected=tfa ? '/arm-trusted-firmware' : (edk2 ? '/edk2' : (top ? '/rpi5-uefi-src' : '/edk2-platforms'))
      unless directory.end_with?(expected)
        warn 'Patch was dispatched to the wrong source tree'
        exit 48
      end
      exit 49 if tfa && ENV['FIXTURE_FAIL']=='patch-tfa'
      exit(ENV['FIXTURE_FAIL']=='patch' ? 42 : 0)
    RUBY
    # The builder controls exercise orchestration. The separate source contract
    # executes the real helper with real Git indexes and worktrees.
    executable.call('ruby', <<~RUBY)
      #!#{RbConfig.ruby}
      require 'json'
      require 'open3'
      unless ARGV.first && ARGV.first.end_with?('/firmware-source.rb')
        exec #{RbConfig.ruby.inspect}, *ARGV
      end
      source=ARGV.fetch(1)
      cache=File.dirname(source)
      lock=cache+'/.ember-firmware.lock'
      abort 'Source helper escaped the builder lease' unless File.read(lock+'/owner').strip==Process.ppid.to_s &&
        File.read(lock+'/source').strip==source
      File.open(ENV.fetch('FIXTURE_TRACE'),'a') { |f| f.puts JSON.generate(['source-helper',ARGV]) }
      if ARGV.include?('--verify-state')
        exit 51 if ENV['FIXTURE_FAIL']=='source-post'
        exit 0
      end
      firmware=File.expand_path('../firmware',File.dirname(ARGV.first))
      File.readlines(firmware+'/series',chomp:true).each do |name|
        owner=name.start_with?('top-') ? '.' : (name.start_with?('tfa-') ? 'arm-trusted-firmware' : (name.start_with?('edk2-') ? 'edk2' : 'edk2-platforms'))
        destination=owner=='.' ? source : source+'/'+owner
        out,err,status=Open3.capture3('patch','-s','-t','-N','-p1','-F0','-d',destination,
          stdin_data:File.binread(firmware+'/'+name))
        STDOUT.write(out); STDERR.write(err)
        exit status.exitstatus unless status.success?
      end
      File.write(cache+'/.ember-firmware-source.json',JSON.generate({'fixture'=>'prepared'}))
    RUBY
    executable.call('python3', <<~RUBY)
      #!#{RbConfig.ruby}
      require 'json'
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['python3', ARGV]) }
      if ARGV.first == '-B' && ARGV[1].end_with?('/firmware-codec-overlay.py')
        require 'fileutils'
        FileUtils.mkdir_p(ARGV.fetch(ARGV.index('--output')+1))
        exit 0
      end
      if ARGV.first == '-c' && ARGV[1].include?('os.path.realpath')
        exec ENV.fetch('FIXTURE_REAL_PYTHON'), *ARGV
      end
      if ARGV == ['-c', 'import elftools']
        exit(ENV['FIXTURE_FAIL'] == 'elftools' ? 40 : 0)
      end
      abort 'Unexpected Python call' unless ARGV.first == '-'
      output = ARGV.fetch(1)
      state = {'image'=>File.exist?(output + '/RPI_EFI.fd'),
               'incoming'=>File.file?(output + '/RPI_EFI.fd.incoming')}
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['receipt-inputs', state]) }
      unless state == {'image'=>false, 'incoming'=>true}
        warn 'Fixture: final image was published before the receipt writer'
        exit 47
      end
      if ENV['FIXTURE_FAIL'] == 'receipt'
        File.write(ARGV[1] + '/firmware-receipt.json', 'partial receipt')
        exit 44
      end
      if ENV['FIXTURE_FAIL']=='lease-owner'
        system ENV.fetch('FIXTURE_REAL_PYTHON'), *ARGV
        status=$?.exitstatus
        File.write(File.dirname(output)+'/.ember-firmware.lock/owner','other owner')
        %w[RPI_EFI.fd RPI_EFI.fd.incoming firmware-receipt.json].each do |file|
          File.write(output+'/'+file,'foreign '+File.basename(output)+'/'+file+10.chr)
        end
        exit status
      end
      exec ENV.fetch('FIXTURE_REAL_PYTHON'), *ARGV
    RUBY
    %w[rm rmdir].each do |tool|
      executable.call(tool, <<~RUBY)
        #!#{RbConfig.ruby}
        if #{tool.inspect}=='rmdir' && ENV['FIXTURE_FAIL']=='lease-rmdir' && ARGV.last.end_with?('/.ember-firmware.lock')
          exit 52
        end
        if #{tool.inspect}=='rm' && ENV['FIXTURE_FAIL']=='source-cleanup' && ARGV.include?('-rf')
          exit 53
        end
        if #{tool.inspect}=='rmdir' && ENV['FIXTURE_UNLOCK_PUBLISH'] && ARGV.last.end_with?('/.ember-firmware.lock')
          require 'fileutils'
          system '/bin/rmdir', *ARGV
          status=$?.exitstatus
          if status==0
            cache=File.dirname(ARGV.last)
            FileUtils.mkdir_p(ARGV.last)
            File.write(ARGV.last+'/owner','new publisher')
            File.write(ARGV.last+'/source',cache+'/rpi5-uefi-src')
            variant=ENV.fetch('FIXTURE_SELECTED')
            %w[RPI_EFI.fd RPI_EFI.fd.incoming firmware-receipt.json].each do |file|
              File.write(cache+'/'+variant+'/'+file,'foreign '+variant+'/'+file+10.chr)
            end
          end
          exit status
        end
        exec #{('/bin/'+tool).inspect}, *ARGV
      RUBY
    end
    executable.call('shasum', <<~RUBY)
      #!#{RbConfig.ruby}
      require 'json'
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['shasum', ARGV]) }
      exec '/usr/bin/shasum', *ARGV
    RUBY
    %w[cp mv].each do |tool|
      executable.call(tool, <<~RUBY)
        #!#{RbConfig.ruby}
        if ENV['FIXTURE_FAIL'] == #{tool == 'cp' ? '"copy"' : '"rename"'}
          File.write(ARGV.last, 'partial copy') if #{tool == 'cp'}
          exit #{tool == 'cp' ? 45 : 46}
        end
        exec #{"/bin/#{tool}".inspect}, *ARGV
      RUBY
    end
    compiler = <<~RUBY
      #!#{RbConfig.ruby}
      require 'json'
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['compiler', ARGV]) }
      exit 39 if ENV['FIXTURE_FAIL'] == 'compiler'
      case ARGV
      when ['--version'] then puts "Fixture GCC " + ENV.fetch('FIXTURE_VERSION')
      when ['-dumpfullversion'] then puts ENV.fetch('FIXTURE_VERSION')
      when ['-dumpmachine'] then puts ENV.fetch('FIXTURE_MACHINE')
      when ['-print-prog-name=as'] then puts ENV.fetch('CROSS_COMPILE') + 'as'
      when ['-print-prog-name=ld'] then puts ENV.fetch('CROSS_COMPILE') + 'ld'
      when ['-print-file-name=include']
        relative = ENV['FIXTURE_FAIL']=='headers-other' ? '../foreign-headers' : '../lib/gcc/' + ENV.fetch('FIXTURE_MACHINE') + '/' + ENV.fetch('FIXTURE_VERSION') + '/include'
        puts File.expand_path(relative, __dir__)
      when ['-dM', '-E', '-x', 'c', '/dev/null']
        puts "#define __GNUC__ " + ENV.fetch('FIXTURE_MAJOR')
        puts '#define __aarch64__ 1' unless ENV['FIXTURE_FAIL'] == 'arch-macro'
        puts '#define __clang__ 1' if ENV['FIXTURE_FAIL'] == 'clang'
      else abort 'Unexpected compiler invocation: ' + ARGV.inspect
      end
    RUBY
    %w[aarch64-none-elf-gcc explicit-gcc].each { |file| executable.call(file, compiler) }
    FileUtils.mkdir_p(directory+'/foreign-headers')
    %w[stddef.h stdint.h stdarg.h].each { |header| File.write(directory+'/foreign-headers/'+header,'foreign fixture header') }
    %w[aarch64-none-elf aarch64--netbsd].each do |machine|
      includes = "#{directory}/lib/gcc/#{machine}/16.2.0/include"
      FileUtils.mkdir_p(includes)
      %w[stddef.h stdint.h stdarg.h].each { |header| File.write(includes+'/'+header, 'fixture compiler header') }
    end
    env = {'PATH'=>"#{tools}:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin",
           'FIXTURE_TRACE'=>"#{directory}/trace", 'FIXTURE_HOST'=>'Darwin',
           'FIXTURE_SELECTED'=>selected, 'FIXTURE_FAIL'=>'', 'FIXTURE_VERSION'=>'16.2.0', 'FIXTURE_MAJOR'=>'16',
           'FIXTURE_MACHINE'=>'aarch64-none-elf', 'FIXTURE_REAL_PYTHON'=>python,
           'PYTHONDONTWRITEBYTECODE'=>'1', 'CROSS_COMPILE'=>"#{tools}/aarch64-none-elf-",
           'EMBERBSD_SDK_SOURCE'=>directory, 'EMBER_HOST_PREFIX'=>directory,
           'EMBER_TARGET_PREFIX'=>directory,
           'EMBER_7ZIP_SOURCE'=>directory, 'EMBER_BROTLI_SOURCE'=>directory,
           'EMBER_7ZIP_TREE'=>'a'*64, 'EMBER_BROTLI_TREE'=>'b'*64,
           'EMBER_TARGET_COMPILER_FAMILY'=>'gcc', 'RPI_BUILD_MAKE'=>tools+'/gmake',
           'EMBER_AARCH64_ELF_TOOLCHAIN'=>''}.merge(values)
    env['CROSS_COMPILE'] = "#{tools}/explicit-" if env['CROSS_COMPILE'] == 'EXPLICIT'
    if env['FIXTURE_PATH_SPACE']
      spaced=directory+'/compiler space/bin'
      FileUtils.mkdir_p(spaced)
      FileUtils.cp(tools+'/aarch64-none-elf-gcc',spaced+'/aarch64-none-elf-gcc')
      env['CROSS_COMPILE']=spaced+'/aarch64-none-elf-'
      env['PATH']=spaced+':'+env['PATH']
    end
    if name.start_with?('profile-search-env-')
      search_keys=%w[CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH COMPILER_PATH GCC_EXEC_PREFIX LD_RUN_PATH LD_LIBRARY_PATH CFLAGS CPPFLAGS CXXFLAGS ASFLAGS LDFLAGS LDLIBS CC CXX CPP AS LD AR NM OBJCOPY OBJDUMP RANLIB STRIP MAKEFLAGS MFLAGS MAKEOVERRIDES]
      search_keys.each { |key| env.delete(key) } # The SDK selector provides the selected-builder environment.
    end
    env['FIXTURE_COMPILER'] = env['CROSS_COMPILE'] + 'gcc'
    args = arguments.map { |arg| arg == 'CACHE' ? cache : (arg == 'CACHE_SPACE' ? cache+' space' : arg) }
    selected_builder=builder
    if env['FIXTURE_ROOT_SPACE']
      selected_builder=directory+'/source space/ember/build-firmware.sh'
      FileUtils.mkdir_p(File.dirname(selected_builder))
      FileUtils.cp(builder,selected_builder)
    end
    if env['FIXTURE_LOCK']
      FileUtils.mkdir_p(cache+'/.ember-firmware.lock')
      File.write(cache+'/.ember-firmware.lock/owner','other owner')
    end
    command = ['/bin/bash', selected_builder, *args]
    out, err, result = Open3.capture3(env, *command)
    begin
      raise "expected status #{expected}, got #{result.exitstatus}: #{out}#{err}" unless result.exitstatus == expected
      variants.each do |variant|
        %w[RPI_EFI.fd RPI_EFI.fd.incoming firmware-receipt.json].each do |file|
          path = "#{cache}/#{variant}/#{file}"
          if variant==selected && (env['FIXTURE_UNLOCK_PUBLISH'] || env['FIXTURE_FAIL']=='lease-owner')
            raise "foreign publisher's #{file} was changed" unless File.read(path)=="foreign #{variant}/#{file}\n"
          elsif preserve || variant != selected
            raise "changed accepted #{variant}/#{file}" unless File.read(path) == "accepted #{variant}/#{file}\n"
          elsif expected != 0
            raise "stale selected #{file} survived" if File.exist?(path)
          elsif file == 'RPI_EFI.fd'
            raise 'new image was not published' unless File.read(path) == "new fixture firmware\n"
          elsif file == 'RPI_EFI.fd.incoming'
            raise 'incoming image remains' if File.exist?(path)
          else
            receipt = JSON.parse(File.read(path))
            identity = receipt.fetch('compiler')
            expected_compiler = env['CROSS_COMPILE'] + 'gcc'
            raise 'compiler path not recorded' unless identity.fetch('path') == expected_compiler
            raise 'compiler version not recorded' unless identity.fetch('version') == '16.2.0'
            raise 'compiler machine not recorded' unless identity.fetch('machine') == env.fetch('FIXTURE_MACHINE')
            raise 'compiler identity not recorded' unless identity.fetch('identity').include?('Fixture GCC 16.2.0')
            raise 'compiler hash differs' unless identity.fetch('sha256') == Digest::SHA256.file(expected_compiler).hexdigest
            raise 'image hash differs' unless receipt.fetch('files').fetch('RPI_EFI.fd') == Digest::SHA256.file("#{cache}/#{variant}/RPI_EFI.fd").hexdigest
          end
        end
      end
      trace = File.exist?(env['FIXTURE_TRACE']) ? File.read(env['FIXTURE_TRACE']) : ''
      events=trace.lines.map { |line| JSON.parse(line) }
      raise 'destructive source reset/clean reached the cache' if events.any? do |event|
        event[0]=='git' && (event[1].include?('reset') || event[1].include?('clean') ||
          event[1].any? { |arg| arg.include?('git reset') || arg.include?('git clean') })
      end

      if %w[build receipt copy rename fill-root].include?(env['FIXTURE_FAIL'])
        raise 'failed rebuild lost build.log' unless File.file?("#{cache}/#{selected}/build.log")
      end
      if expected == 0 && !preserve
        hashes=events.count { |event| event[0]=='shasum' && event[1].last==env['FIXTURE_COMPILER'] }
        raise "selected compiler was hashed #{hashes} times" unless hashes == 1
      end
      raise 'prepare used a compiler or Python' if preserve && args.include?('--prepare') && trace.match?(/compiler|python3|"build"/)
      if name.start_with?('gcc-') && expected != 0
        raise 'invalid compiler reached Git or build' if trace.include?('["git"') || trace.include?('["build"')
      end
      if expected == 0 && !preserve
        call = trace.lines.find { |line| line.start_with?('["build"') }
        flags = call && JSON.parse(call).fetch(1)
        raise 'variant build arguments changed' unless flags && flags.length==6 && flags[0,3]==['--model','5','--tfa-flags'] && flags[4]=='--edk2-flags'
        raise 'TF-A must use the verified GCC driver' unless flags[3].include?("CC=#{env.fetch('FIXTURE_COMPILER')} LD=#{env.fetch('FIXTURE_COMPILER')}")
        raise 'missing strict compiler flags' unless flags[3].include?('CFLAGS+=-ffreestanding') && flags[3].include?('LDFLAGS+=-Wl,-nostdlib')
        raise 'missing strict EDK2 macros' unless flags[5].include?('EMBER_FIRMWARE_ROOT=') && flags[5].include?('EMBER_GCC_INCLUDE=') && flags[5].include?('EMBER_FIRMWARE_SPECS=')
        if selected!='firmware'
          raise 'lost variant TF-A flag' unless flags[3].start_with?('LOG_LEVEL=0 ')
          raise 'lost console flag' unless flags[5].include?('-D RP1_UART_CONSOLE=TRUE')
          raise 'lost trace flag' if selected.end_with?('trace') && !flags[5].include?('-D CM5_BOOT_TRACE=TRUE')
        end
        record=JSON.parse(File.read("#{cache}/#{selected}/firmware-receipt.json")).fetch('freestanding')
        raise 'empty root was not removed' if File.exist?(record.fetch('empty_root'))
        raise 'profile receipt has the wrong builtin include' unless record.fetch('include')=="#{directory}/lib/gcc/#{env.fetch('FIXTURE_MACHINE')}/16.2.0/include"
      end
      checks += 1
      puts "PASS: #{name}"
    rescue StandardError => error
      failures << "#{name}: #{error.message}"
      warn "FAIL: #{failures.last}"
    ensure
      if evidence
        File.write("#{evidence}/#{name}.json", JSON.pretty_generate({command:command, exit_status:result.exitstatus,
          stdout:out, stderr:err, trace: File.exist?(env['FIXTURE_TRACE']) ? File.read(env['FIXTURE_TRACE']) : '',
          build_log: File.file?("#{cache}/#{selected}/build.log") ? File.read("#{cache}/#{selected}/build.log") : '',
          fake_build_source: File.file?("#{cache}/rpi5-uefi-src/build.sh") ? File.read("#{cache}/rpi5-uefi-src/build.sh") : ''}) + "\n")
      end
    end
  end
  {'standard'=>['CACHE'], 'console'=>['CACHE','--rp1-console'], 'trace'=>['CACHE','--rp1-trace']}.each do |name,args|
    selected = {'standard'=>'firmware','console'=>'firmware-rp1-console','trace'=>'firmware-rp1-trace'}.fetch(name)
    # SDK preflight is tested by firmware-toolchain.py before this selected-builder fixture.
    test.call("git-failure-#{name}", args, {'FIXTURE_FAIL'=>'git'}, 41, selected)
    test.call("patch-failure-#{name}", args, {'FIXTURE_FAIL'=>'patch'}, 42, selected)
    test.call("build-failure-#{name}", args, {'FIXTURE_FAIL'=>'build'}, 43, selected)
    test.call("receipt-failure-#{name}", args, {'FIXTURE_FAIL'=>'receipt'}, 44, selected)
    test.call("copy-failure-#{name}", args, {'FIXTURE_FAIL'=>'copy'}, 45, selected)
    test.call("rename-failure-#{name}", args, {'FIXTURE_FAIL'=>'rename'}, 46, selected)
    test.call("success-#{name}", args, {}, 0, selected)
    test.call("prepare-#{name}", ['--prepare',*args], {'FIXTURE_HOST'=>'Unsupported'}, 0, selected, true)
    test.call("prepare-failure-#{name}", [*args,'--prepare'], {'FIXTURE_FAIL'=>'git'}, 41, selected, true)
  end
  [['gcc-old',{'FIXTURE_VERSION'=>'15.2.0','FIXTURE_MAJOR'=>'15'}],
   ['gcc-new',{'FIXTURE_VERSION'=>'17.0.0','FIXTURE_MAJOR'=>'17'}],
   ['gcc-malformed',{'FIXTURE_VERSION'=>'16.not-a-version'}],
   ['gcc-wrong-machine',{'FIXTURE_MACHINE'=>'x86_64-linux-gnu'}],
   ['gcc-clang',{'FIXTURE_FAIL'=>'clang'}],
   ['gcc-arch-macro',{'FIXTURE_FAIL'=>'arch-macro'}],
      ['gcc-explicit-missing',{'CROSS_COMPILE'=>'/no/such/fixture-prefix-'}]].each { |name,env| test.call(name,['CACHE','--rp1-console'],env,1,'firmware-rp1-console') }
  test.call('gcc-command-failure',['CACHE','--rp1-console'],{'FIXTURE_FAIL'=>'compiler'},39,'firmware-rp1-console')
  test.call('explicit-netbsd-identity-only',['CACHE'],{'CROSS_COMPILE'=>'EXPLICIT','FIXTURE_MACHINE'=>'aarch64--netbsd'},0)
  test.call('profile-cache-space',['CACHE_SPACE'],{},1,'firmware',true)
  test.call('profile-prefix-space',['CACHE'],{'CROSS_COMPILE'=>'/compiler space/prefix-'},1,'firmware',true)
  test.call('profile-toolchain-space',['CACHE'],{'EMBER_TARGET_PREFIX'=>'/compiler space'},1,'firmware',true)
  test.call('profile-root-space',['CACHE'],{'FIXTURE_ROOT_SPACE'=>'1'},1,'firmware',true)
  test.call('profile-resolved-compiler-space',['CACHE'],{'FIXTURE_PATH_SPACE'=>'1'},1,'firmware',true)
  test.call('profile-wrong-header-owner',['CACHE'],{'FIXTURE_FAIL'=>'headers-other'},1)
  test.call('profile-empty-root-change',['CACHE'],{'FIXTURE_FAIL'=>'fill-root'},1)
  search_env=%w[CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH COMPILER_PATH GCC_EXEC_PREFIX LD_RUN_PATH LD_LIBRARY_PATH CFLAGS CPPFLAGS CXXFLAGS ASFLAGS LDFLAGS LDLIBS CC CXX CPP AS LD AR NM OBJCOPY OBJDUMP RANLIB STRIP MAKEFLAGS MFLAGS MAKEOVERRIDES].to_h { |key| [key,'/unsupported/firmware-search'] }
  test.call('profile-search-env-standard',['CACHE'],search_env,0)
  test.call('profile-search-env-console',['CACHE','--rp1-console'],search_env,0,'firmware-rp1-console')
  test.call('profile-search-env-trace',['CACHE','--rp1-trace'],search_env,0,'firmware-rp1-trace')
  [['standard',[], 'firmware'],['console',['--rp1-console'],'firmware-rp1-console'],
   ['trace',['--rp1-trace'],'firmware-rp1-trace']].each do |name,args,selected|
    test.call('tfa-owner-success-'+name,['CACHE',*args],{},0,selected)
    test.call('tfa-owner-failure-'+name,['CACHE',*args],{'FIXTURE_FAIL'=>'patch-tfa'},49,selected)
  end
  test.call('late-unlock-other-publisher',['CACHE'],{'FIXTURE_FAIL'=>'build','FIXTURE_UNLOCK_PUBLISH'=>'1'},43)
  test.call('late-lease-rmdir',['CACHE'],{'FIXTURE_FAIL'=>'lease-rmdir'},1)
  test.call('late-source-cleanup',['CACHE'],{'FIXTURE_FAIL'=>'source-cleanup'},1)
  test.call('late-lease-owner',['CACHE'],{'FIXTURE_FAIL'=>'lease-owner'},1)
  test.call('source-post-refusal',['CACHE'],{'FIXTURE_FAIL'=>'source-post'},51)
  test.call('source-manifest-change',['CACHE'],{'FIXTURE_FAIL'=>'manifest-changed'},1)
  test.call('cache-lease-contention',['CACHE'],{'FIXTURE_LOCK'=>'1'},1,'firmware',true)
  test.call('prepare-lease-contention',['CACHE','--prepare'],{'FIXTURE_LOCK'=>'1'},1,'firmware',true)
  test.call('tfa-owner-prepare',['CACHE','--prepare'],{},0,'firmware',true)
  [['input-empty',[]],['input-prepare-only',['--prepare']],['input-relative',['relative']],['input-unknown-option',['CACHE','--unknown']],
   ['input-extra',['CACHE','--rp1-console','extra']],['input-prepare-unknown',['CACHE','--prepare','--unknown']]].each do |name,args|
    test.call(name,args,{},1,'firmware',true)
  end
end
puts "#{failures.empty? ? 'PASS' : 'FAIL'}: #{checks} firmware controls passed; #{failures.length} failed; fake services only"
exit(failures.empty? ? 0 : 1)
