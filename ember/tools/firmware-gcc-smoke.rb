#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted real GCC16 compiler contract.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'

abort 'usage: firmware-gcc-smoke.rb GCC PINNED_INPUTS EMPTY_OUTPUT' unless ARGV.length == 3
gcc, upstream, output = ARGV.map { |path| File.expand_path(path) }
root = File.expand_path('../..', __dir__)
compiler_sha256 = Digest::SHA256.file(gcc).hexdigest
profile = root + '/ember/firmware/gcc-profile.sh'
specs = root + '/ember/firmware/gcc.specs'
fixture = root + '/ember/tests/firmware-gcc'
abort 'Output must be empty' if File.exist?(output) && !Dir.empty?(output)
FileUtils.mkdir_p(output)
commands = []
checks = 0
run = lambda do |name, command, env = {}, success = true, input = nil|
  stdout, stderr, status = Open3.capture3(env, *command, stdin_data: input.to_s)
  commands << {name:name, argv:command, environment:env, status:status.exitstatus,
               stdout:stdout, stderr:stderr, stdin:input}
  File.write(output+'/commands.json',JSON.pretty_generate(commands)+"\n")
  abort "#{name}: unexpected status #{status.exitstatus}: #{stderr}" unless status.success? == success
  stdout + stderr
end
query = lambda do |name, args, env = {}, success = true|
  run.call(name, ['/bin/bash','-c','source "$1"; shift; firmware_gcc_query "$@"',
                  'firmware-query',profile,gcc,*args],env,success)
end
check = lambda do |name, condition|
  abort 'FAIL: '+name unless condition
  checks += 1
  puts 'PASS: '+name
end
machine = query.call('machine',['-dumpmachine']).strip
version = query.call('version',['-dumpfullversion']).strip
check.call('AArch64 GCC16', machine.start_with?('aarch64-') && version.match?(/^16\./))
prefix = File.dirname(gcc)+'/'+machine+'-'
readelf = prefix+'readelf'
ar = prefix+'ar'
tools_path = upstream+'/edk2/BaseTools/Conf/tools_def.template'
platform_file = 'Platform/RaspberryPi/RPi5/RPi5.dsc'
dsc_path = output+'/platform/'+platform_file
FileUtils.mkdir_p(File.dirname(dsc_path))
# --prepare already applies the series. Always start this isolated control
# from the original pinned DSC, without changing the shared working tree.
pinned_dsc=run.call('pinned-DSC',['git','--no-optional-locks','-c','protocol.allow=never',
  '-C',upstream+'/edk2-platforms','show',
  'c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed:'+platform_file],
  {'GIT_NO_LAZY_FETCH'=>'1','GIT_OPTIONAL_LOCKS'=>'0','GIT_TERMINAL_PROMPT'=>'0'})
File.binwrite(dsc_path,pinned_dsc)
# Apply only this file's hunks. This does not validate the complete series.
File.foreach(root+'/ember/firmware/series') do |entry|
  patch = File.read(root+'/ember/firmware/'+entry.strip)
  sections = patch.split(/(?=^--- a\/)/).select { |section| section.start_with?('--- a/'+platform_file+"\n") }
  next if sections.empty?
  run.call('DSC-'+entry.strip,['patch','-s','-p1','-F0','-d',output+'/platform'],{},true,sections.join)
end
definitions = {}
File.foreach(tools_path) do |line|
  match = line.strip.match(/^(?:DEFINE\s+)?(\S+)\s*=\s*(.*)$/)
  definitions[match[1]] = match[2] if match
end
expand = nil
expand = lambda do |text, variables, depth = 0|
  abort 'Recursive tools definition' if depth > 20
  text = text.gsub(/DEF\(([^)]+)\)/) { expand.call(definitions.fetch($1),variables,depth+1) }
  text.gsub(/\$\(([^)]+)\)/) { variables.fetch($1) }
end
empty = Dir.mktmpdir('gcc-empty-',output)
begin
  text = run.call('profile',['/bin/bash','-c',
    'source "$1"; firmware_gcc_profile "$2" "$3" "$4" "$5" "$6"; printf "CPP %s\\n" "${firmware_gcc_cpp[@]}"; printf "LINK %s\\n" "${firmware_gcc_link[@]}"; printf "INCLUDE %s\\n" "$firmware_gcc_include"',
    'firmware-profile',profile,gcc,machine,version,empty,specs])
  cpp = text.lines.grep(/^CPP /).map { |line| line.chomp.delete_prefix('CPP ') }
  link = text.lines.grep(/^LINK /).map { |line| line.chomp.delete_prefix('LINK ') }
  include_path = text.lines.grep(/^INCLUDE /).first.strip.delete_prefix('INCLUDE ')
  vars = {'WORKSPACE'=>upstream+'/edk2','EDK_TOOLS_PATH'=>upstream+'/edk2/BaseTools',
          'PLATFORM_FLAGS'=>'','BASE_NAME'=>'firmware_smoke','MODULE_NAME'=>'firmware_smoke',
          'IMAGE_ENTRY_POINT'=>'_start','DEST_DIR_DEBUG'=>output,
          'EMBER_FIRMWARE_ROOT'=>empty,'EMBER_GCC_INCLUDE'=>include_path,
          'EMBER_FIRMWARE_SPECS'=>specs}
  dsc_options = {}
  File.foreach(dsc_path) do |line|
    match = line.match(/^\s+GCC:\*_\*_AARCH64_(\S+)_FLAGS\s+=\s+(.*EMBER_.*)$/)
    dsc_options[match[1]] = Shellwords.split(expand.call(match[2].strip,vars)) if match
  end
  check.call('DSC appends the shared compiler profile',dsc_options.fetch('CC') == cpp)
  check.call('DSC appends the shared linker profile',dsc_options.fetch('DLINK') == link)
  check.call('all EDK2 preprocessors retain the profile',%w[PP ASM VFRPP ASLPP ASLCC DTCPP].all? { |tool| dsc_options.fetch(tool)==cpp })
  edk_cc = Shellwords.split(expand.call(definitions.fetch('RELEASE_GCC_AARCH64_CC_FLAGS'),vars))
  edk_ld = Shellwords.split(expand.call(definitions.fetch('RELEASE_GCC_AARCH64_DLINK_FLAGS'),vars))
  edk_ld += Shellwords.split(expand.call(definitions.fetch('*_GCC_AARCH64_DLINK2_FLAGS'),vars))
  check.call('upstream EDK2 LTO is retained',edk_cc.include?('-flto') && edk_ld.include?('-flto') && edk_ld.include?('-llto-aarch64'))

  # Use the pinned TF-A make fragment and the real top-level token API.
  make = ENV.fetch('FIRMWARE_TEST_GMAKE', 'gmake')
  make_args = ['-f',upstream+'/arm-trusted-firmware/make_helpers/cflags.mk','-n',
    '--eval=firmware-flags: ; @echo CC=$(CC) FLAGS=$(TF_CFLAGS) LINK=$(LDFLAGS)',
    'firmware-flags','ARCH=aarch64','aarch64-cc-id=gnu-gcc','aarch64-ld-id=gnu-gcc',
      'MAKE_HELPERS_DIRECTORY='+upstream+'/arm-trusted-firmware/make_helpers/', 'MARCH_DIRECTIVE=-march=armv8-a',
      'ENABLE_FEAT_MORELLO=0', 'DEBUG=0',
    'TF_CFLAGS_aarch64=-march=armv8-a -mstrict-align -flto',
    *cpp.map { |flag| 'CFLAGS+='+flag }, *link.map { |flag| 'LDFLAGS+='+flag }]
  inherited = {'CFLAGS'=>'-I/firmware-forbidden-headers',
    'CPPFLAGS'=>'-I/firmware-forbidden-cpp','LDFLAGS'=>'-L/firmware-forbidden-libraries',
    'MAKEFLAGS'=>'-e','CC'=>'clang'}
  baseline_make = run.call('baseline-inherited-make',[make,*make_args],inherited)
  check.call('baseline inherits caller flags and compiler overrides',
    baseline_make.include?('/firmware-forbidden-headers') &&
    baseline_make.include?('/firmware-forbidden-cpp') &&
    baseline_make.include?('/firmware-forbidden-libraries') && baseline_make.include?('CC=clang'))
  strict_make = run.call('strict-pinned-TFA-make', ['/bin/bash','-c',
    'source "$1"; shift; firmware_gcc_query "$@"','firmware-make',profile,make,*make_args],inherited)
  check.call('strict clears inherited compiler and make overrides',
    !strict_make.include?('/firmware-forbidden-') && !strict_make.include?('CC=clang'))
  check.call('TF-A assignments retain architecture, LTO and every profile token',
    %w[-march=armv8-a -mstrict-align -flto].all? { |flag| strict_make.include?(flag) } &&
    (cpp+link).all? { |flag| strict_make.include?(flag) })

  tool_args=['-f',upstream+'/arm-trusted-firmware/make_helpers/toolchain.mk','-n',
    '--eval=firmware-tools: ; @echo CC=$(aarch64-cc) LD=$(aarch64-ld) ID=$(aarch64-ld-id)',
    'firmware-tools','CROSS_COMPILE='+prefix]
  raw_ld=prefix+'ld'
  baseline_tools=run.call('baseline-TFA-tool-selection',[make,*tool_args],{'LD'=>raw_ld})
  baseline_tool_words=Shellwords.split(baseline_tools.lines.find { |line| line.start_with?('echo ') }.to_s)
  check.call('baseline TF-A allows inherited raw ld',baseline_tool_words.include?('LD='+raw_ld) && baseline_tool_words.include?('ID=gnu-ld'))
  strict_tools=run.call('strict-TFA-tool-selection',['/bin/bash','-c',
    'source "$1"; shift; firmware_gcc_query "$@"','firmware-tools',profile,make,*tool_args,
    'CC='+gcc,'LD='+gcc],{'LD'=>raw_ld,'CC'=>'clang','MAKEFLAGS'=>'-e'})
  strict_tool_words=Shellwords.split(strict_tools.lines.find { |line| line.start_with?('echo ') }.to_s)
  check.call('TF-A selects the validated GCC compiler and linker driver',
    strict_tool_words.include?('CC='+gcc) && strict_tool_words.include?('LD='+gcc) && strict_tool_words.include?('ID=gnu-gcc'))

  # Minimal ELF controls expose defaults without claiming a full TF-A build.
  minimal = output+'/minimal.c'
  File.write(minimal,"void _start(void) {}\n")
  tfa = %w[-nostdinc -ffreestanding -fno-common -Os -std=gnu99]
  query.call('baseline-tfa-elf',[*tfa,'-nostdlib','-Wl,-e,_start',minimal,'-o',output+'/baseline-tfa.elf'])
  run.call('baseline-minimal-ELF-metadata',[readelf,'-lW','-dW',output+'/baseline-tfa.elf'])
  query.call('baseline-runtime-input',[*tfa,'-nostdlib','-Wl,-e,_start','-Wl,--no-as-needed',
    minimal,'-lgcc_s','-o',output+'/baseline-runtime.elf'])
  baseline = run.call('baseline-runtime-ELF-metadata',[readelf,'-lW','-dW',output+'/baseline-runtime.elf'])
  check.call('baseline permits an accidental OS runtime',baseline.include?('/usr/libexec/ld.elf_so') && baseline.include?('NEEDED'))
  query.call('reject-OS-runtime',[*cpp,*link,'-Wl,-e,_start','-Wl,--no-as-needed',
    minimal,'-lgcc_s','-o',output+'/rejected-runtime.elf'],{},false)
  check.call('strict rejects the implicit OS runtime',!File.exist?(output+'/rejected-runtime.elf'))
  baseline_driver = query.call('baseline-driver',['-###','-nostdlib',minimal,'-o','/dev/null'])
  check.call('baseline exposes runtime search paths',baseline_driver.include?(machine+'/lib') && baseline_driver.include?('/sysroot/usr/lib'))
  query.call('baseline-static-runtime-search',[*cpp,'-nostdlib','-static','-Wl,-e,_start',
    '-Wl,-Map,'+output+'/baseline-static.map',minimal,'-lstdc++','-o',output+'/baseline-static.elf'])
  check.call('baseline static lookup reaches the compiler runtime',File.read(output+'/baseline-static.map').include?(machine+'/lib/libstdc++.a'))
  query.call('reject-static-runtime-search',[*cpp,*link,'-Wl,-e,_start',minimal,
    '-lstdc++','-o',output+'/rejected-static.elf'],{},false)
  check.call('strict removes implicit static runtime lookup',!File.exist?(output+'/rejected-static.elf'))

  sources = %w[start arithmetic].map { |name| fixture+'/'+name+'.c' }
  %w[tfa edk2].each do |mode|
    directory=output+'/'+mode
    FileUtils.mkdir_p(directory)
    cc = mode=='edk2' ? edk_cc+dsc_options.fetch('CC')+['-DFIRMWARE_EDK2'] : tfa+cpp+['-flto']
    ld = mode=='edk2' ? edk_ld+dsc_options.fetch('DLINK') : ['-flto',*link]
    objects = sources.map do |source|
      object=directory+'/'+File.basename(source,'.c')+'.o'
      query.call(mode+'-compile-'+File.basename(source),[*cc,'-DFIRMWARE_STRICT','-I'+fixture,
        '-MD','-MF',object+'.d','-c',source,'-o',object])
      sections=run.call(mode+'-LTO-object',[readelf,'-SW',object])
      check.call(mode+' uses real LTO objects',sections.include?('.gnu.lto_'))
      deps=File.read(object+'.d')
      check.call(mode+' excludes OS headers',!deps.include?('/sysroot/') && !deps.include?('/usr/include/'))
      check.call(mode+' retains compiler standard headers',deps.include?(include_path+'/stddef.h') && deps.include?(include_path+'/stdarg.h') && deps.include?(include_path+'/stdint-gcc.h'))
      object
    end
    elf=directory+'/firmware.elf'
    map=directory+'/firmware.map'
    query.call(mode+'-link',[*ld,'-Wl,-e,_start','-Wl,--trace','-Wl,-Map,'+map,*objects,'-o',elf])
    metadata=run.call(mode+'-ELF-metadata',[readelf,'-hW','-lW','-dW',elf])
    check.call(mode+' ELF is AArch64',metadata.include?('AArch64'))
    check.call(mode+' ELF has no OS loader or dynamic dependencies',!metadata.match?(/INTERP|NEEDED|RPATH|RUNPATH/))
    check.call(mode+' map excludes OS runtime',!File.read(map).match?(/\/sysroot\/|libgcc_s|libstdc\+\+|\/crt[^\/]*\.o/))
    driver=query.call(mode+'-driver',['-###',*ld,*objects,'-o','/dev/null'])
    collect=driver.lines.find { |line| line.include?('/collect2 ') }
    check.call(mode+' driver excludes implicit runtime directories',collect && !collect.match?(/ -L\S*#{Regexp.escape(machine)}\/lib|\/sysroot\//))
  end

  foreign=output+'/foreign'
  FileUtils.mkdir_p(foreign)
  File.write(foreign+'/firmware_forbidden.h',"#define FIRMWARE_FORBIDDEN 1\n")
  foreign_source=foreign+'/header.c'
  File.write(foreign_source,"#include <firmware_forbidden.h>\nint foreign_header(void) { return FIRMWARE_FORBIDDEN; }\n")
  env={'CPATH'=>foreign,'C_INCLUDE_PATH'=>foreign,'LIBRARY_PATH'=>foreign}
  run.call('baseline-foreign-header',[gcc,'-c',foreign_source,'-o',foreign+'/baseline.o'],env)
  query.call('reject-foreign-header',[*cpp,'-c',foreign_source,'-o',foreign+'/rejected.o'],env,false)
  check.call('foreign header is rejected',!File.exist?(foreign+'/rejected.o'))
  query.call('foreign-library-object',[*cpp,'-c',fixture+'/foreign.c','-o',foreign+'/foreign.o'])
  run.call('foreign-library-archive',[ar,'rcs',foreign+'/libfirmware_foreign.a',foreign+'/foreign.o'])
  foreign_use=foreign+'/use.c'
  File.write(foreign_use,"extern unsigned long firmware_external(unsigned long);\nvolatile unsigned long result;\nvoid _start(void) { result=firmware_external(3); }\n")
  run.call('baseline-foreign-library',[gcc,'-nostdlib','-Wl,-e,_start',foreign_use,
    '-L'+foreign,'-lfirmware_foreign','-o',foreign+'/baseline.elf'],env)
  query.call('reject-foreign-library',[*cpp,*link,'-Wl,-e,_start',foreign_use,
    '-lfirmware_foreign','-o',foreign+'/rejected.elf'],env,false)
  check.call('foreign library is rejected',!File.exist?(foreign+'/rejected.elf'))
  query.call('explicit-firmware-library',[*cpp,*link,'-Wl,-e,_start',foreign_use,
    '-L'+foreign,'-lfirmware_foreign','-o',foreign+'/explicit.elf'],env)
  explicit=run.call('explicit-library-ELF',[readelf,'-lW','-dW',foreign+'/explicit.elf'])
  check.call('explicit firmware library remains supported',!explicit.match?(/INTERP|NEEDED|RPATH|RUNPATH/))
  hostile=env.merge('COMPILER_PATH'=>'/firmware-not-a-toolchain/', 'GCC_EXEC_PREFIX'=>'/firmware-not-a-toolchain/')
  query.call('reject-search-environment',[*cpp,'-c',fixture+'/arithmetic.c','-o',foreign+'/clean.o'],hostile)
  check.call('compiler internals are isolated from inherited search variables',File.file?(foreign+'/clean.o'))
  check.call('empty search root remains empty',Dir.empty?(empty) && !File.symlink?(empty))
  check.call('selected compiler is unchanged',Digest::SHA256.file(gcc).hexdigest==compiler_sha256)
  receipt={compiler:gcc,version:version,machine:machine,compiler_sha256:compiler_sha256,
    profile_sha256:Digest::SHA256.file(profile).hexdigest,specs_sha256:Digest::SHA256.file(specs).hexdigest,
    tools_sha256:Digest::SHA256.file(tools_path).hexdigest,dsc_sha256:Digest::SHA256.file(dsc_path).hexdigest,
    input_sha256:([tools_path,dsc_path,upstream+'/edk2-platforms/'+platform_file,
      upstream+'/edk2/BaseTools/Scripts/GccBase.lds',
      upstream+'/edk2/BaseTools/Bin/GccLto/liblto-aarch64.a',
      upstream+'/arm-trusted-firmware/make_helpers/cflags.mk',
      upstream+'/arm-trusted-firmware/make_helpers/march.mk'] +
      Dir[upstream+'/arm-trusted-firmware/make_helpers/{toolchain.mk,utilities.mk,toolchains/*.mk}'] + Dir[fixture+'/*']).to_h { |path| [path,Digest::SHA256.file(path).hexdigest] },
    checks:checks,scope:'Real GCC16 compile/link and ELF/header/LTO controls; no complete TF-A/EDK2 build or target execution.'}
  File.write(output+'/receipt.json',JSON.pretty_generate(receipt)+"\n")
ensure
  Dir.rmdir(empty)
end
puts "PASS: #{checks} real firmware GCC controls"
