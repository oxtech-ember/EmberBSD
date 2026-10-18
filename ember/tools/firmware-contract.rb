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
builder = ENV.fetch('FIRMWARE_BUILDER', "#{root}/ember/build-firmware.sh")
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
    %w[gmake iasl dtc].each { |tool| executable.call(tool, "#!/bin/sh\nexit 0\n") }
    fake_build = <<~RUBY
      #!#{RbConfig.ruby}
      require 'json'
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['build', ARGV]) }
      exit 43 if ENV['FIXTURE_FAIL'] == 'build'
      File.write('RPI_EFI.fd', "new fixture firmware\n")
      if ENV['FIXTURE_FAIL'] == 'compiler-changed'
        File.open(ENV.fetch('FIXTURE_COMPILER'), 'a') { |f| f.puts '# changed during fixture build' }
      end
    RUBY
    executable.call('git', <<~RUBY)
      #!#{RbConfig.ruby}
      require 'fileutils'
      require 'json'
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['git', ARGV]) }
      exit 41 if ENV['FIXTURE_FAIL'] == 'git' && ARGV.include?('fetch')
      if ARGV.first == 'clone'
        destination = ARGV.last
        FileUtils.mkdir_p(%w[.git edk2 edk2-platforms].map { |p| destination + '/' + p })
        File.write(destination + '/build.sh', #{fake_build.dump})
        File.chmod(0o700, destination + '/build.sh')
      end
    RUBY
    executable.call('patch', "#!/bin/sh\nprintf '%s\\n' fake-patch >> \"$FIXTURE_TRACE\"\n[ \"$FIXTURE_FAIL\" != patch ] || exit 42\n")
    executable.call('python3', <<~RUBY)
      #!#{RbConfig.ruby}
      require 'json'
      File.open(ENV.fetch('FIXTURE_TRACE'), 'a') { |f| f.puts JSON.generate(['python3', ARGV]) }
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
      exec ENV.fetch('FIXTURE_REAL_PYTHON'), *ARGV
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
      when ['-dM', '-E', '-x', 'c', '/dev/null']
        puts "#define __GNUC__ " + ENV.fetch('FIXTURE_MAJOR')
        puts '#define __aarch64__ 1' unless ENV['FIXTURE_FAIL'] == 'arch-macro'
        puts '#define __clang__ 1' if ENV['FIXTURE_FAIL'] == 'clang'
      else abort 'Unexpected compiler invocation: ' + ARGV.inspect
      end
    RUBY
    %w[aarch64-none-elf-gcc explicit-gcc].each { |file| executable.call(file, compiler) }
    env = {'PATH'=>"#{tools}:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin",
           'FIXTURE_TRACE'=>"#{directory}/trace", 'FIXTURE_HOST'=>'Darwin',
           'FIXTURE_FAIL'=>'', 'FIXTURE_VERSION'=>'16.2.0', 'FIXTURE_MAJOR'=>'16',
           'FIXTURE_MACHINE'=>'aarch64-none-elf', 'FIXTURE_REAL_PYTHON'=>python,
           'PYTHONDONTWRITEBYTECODE'=>'1', 'CROSS_COMPILE'=>'',
           'EMBER_AARCH64_ELF_TOOLCHAIN'=>''}.merge(values)
    env['CROSS_COMPILE'] = "#{tools}/explicit-" if env['CROSS_COMPILE'] == 'EXPLICIT'
    env['FIXTURE_COMPILER'] = env['CROSS_COMPILE'].empty? ? "#{tools}/aarch64-none-elf-gcc" : "#{tools}/explicit-gcc"
    args = arguments.map { |arg| arg == 'CACHE' ? cache : arg }
    command = ['/bin/bash', builder, *args]
    out, err, result = Open3.capture3(env, *command)
    begin
      raise "expected status #{expected}, got #{result.exitstatus}: #{out}#{err}" unless result.exitstatus == expected
      variants.each do |variant|
        %w[RPI_EFI.fd RPI_EFI.fd.incoming firmware-receipt.json].each do |file|
          path = "#{cache}/#{variant}/#{file}"
          if preserve || variant != selected
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
            expected_compiler = env['CROSS_COMPILE'].empty? ? "#{tools}/aarch64-none-elf-gcc" : "#{tools}/explicit-gcc"
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
      if %w[build receipt copy rename compiler-changed].include?(env['FIXTURE_FAIL'])
        raise 'failed rebuild lost build.log' unless File.file?("#{cache}/#{selected}/build.log")
      end
      if env['FIXTURE_FAIL'] == 'compiler-changed'
        raise 'compiler replacement was not diagnosed' unless err.include?('target compiler changed during firmware build')
      end
      raise 'prepare used a compiler or Python' if preserve && args.include?('--prepare') && trace.match?(/compiler|python3|"build"|fake-patch/)
      if name.start_with?('gcc-') && expected != 0
        raise 'invalid compiler reached Git or build' if trace.include?('["git"') || trace.include?('["build"')
      end
      if expected == 0 && !preserve
        call = trace.lines.find { |line| line.start_with?('["build"') }
        flags = selected == 'firmware' ? [] : ['--tfa-flags', 'LOG_LEVEL=0', '--edk2-flags',
          selected.end_with?('trace') ? '-D RP1_UART_CONSOLE=TRUE -D CM5_BOOT_TRACE=TRUE' : '-D RP1_UART_CONSOLE=TRUE']
        raise 'variant build arguments changed' unless call && JSON.parse(call) == ['build', ['--model', '5', *flags]]
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
    test.call("early-host-#{name}", args, {'FIXTURE_HOST'=>'Unsupported'}, 1, selected)
    test.call("early-elftools-#{name}", args, {'FIXTURE_FAIL'=>'elftools'}, 1, selected)
    test.call("git-failure-#{name}", args, {'FIXTURE_FAIL'=>'git'}, 41, selected)
    test.call("patch-failure-#{name}", args, {'FIXTURE_FAIL'=>'patch'}, 42, selected)
    test.call("build-failure-#{name}", args, {'FIXTURE_FAIL'=>'build'}, 43, selected)
    test.call("receipt-failure-#{name}", args, {'FIXTURE_FAIL'=>'receipt'}, 44, selected)
    test.call("copy-failure-#{name}", args, {'FIXTURE_FAIL'=>'copy'}, 45, selected)
    test.call("rename-failure-#{name}", args, {'FIXTURE_FAIL'=>'rename'}, 46, selected)
    test.call("compiler-changed-#{name}", args, {'FIXTURE_FAIL'=>'compiler-changed'}, 1, selected)
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
   ['gcc-command-failure',{'FIXTURE_FAIL'=>'compiler'}],
   ['gcc-explicit-missing',{'CROSS_COMPILE'=>'/no/such/fixture-prefix-'}]].each { |name,env| test.call(name,['CACHE','--rp1-console'],env,1,'firmware-rp1-console') }
  test.call('explicit-netbsd-identity-only',['CACHE'],{'CROSS_COMPILE'=>'EXPLICIT','FIXTURE_MACHINE'=>'aarch64--netbsd'},0)
  [['input-empty',[]],['input-prepare-only',['--prepare']],['input-relative',['relative']],['input-unknown-option',['CACHE','--unknown']],
   ['input-extra',['CACHE','--rp1-console','extra']],['input-prepare-unknown',['CACHE','--prepare','--unknown']]].each do |name,args|
    test.call(name,args,{},1,'firmware',true)
  end
end
puts "#{failures.empty? ? 'PASS' : 'FAIL'}: #{checks} firmware controls passed; #{failures.length} failed; fake services only"
exit(failures.empty? ? 0 : 1)
