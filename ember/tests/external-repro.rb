#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted real external-GCC reproducible debug regression.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'digest'
abort 'Usage: external-repro.rb NBMAKE_WRAPPER FULL_SOURCE GCC16_PREFIX' unless ARGV.length == 3
make, os, prefix = ARGV.map { |p| File.realpath(p) }
cc = prefix + '/bin/x86_64--netbsd-gcc'
readelf = prefix + '/bin/x86_64--netbsd-readelf'
version, status = Open3.capture2(cc, '-dumpfullversion')
abort 'This fixture requires the selected GCC 16.2.0' unless status.success? && version.strip == '16.2.0'
Dir.mktmpdir('ember-external-repro-', '/tmp') do |work|
  work = File.realpath(work)
  fixture = work + '/repro.mk'
  File.write(fixture, <<~MAKE)
    .include "#{os}/share/mk/bsd.own.mk"
    CFLAGS+= -g -O0
    CPPFLAGS+= -I${DESTDIR}/usr/include -I${X11SRCDIR}/include
    .include "#{os}/share/mk/bsd.sys.mk"
    .PHONY: check
    check:
    \t${CC} ${CPPFLAGS} ${CFLAGS} -c ${NETBSDSRCDIR}/probe.c -o probe.o
    \t${CC} ${CPPFLAGS} ${CFLAGS} -c ${NETBSDOBJDIR}/gen/generated.c -o generated.o
  MAKE
  outputs = []
  2.times do |index|
    root = work + '/root-' + index.to_s
    src = root + '/source'; obj = index.zero? ? root + '/objects' : src + '/obj.custom'
    dest = root + '/dest'; x11 = root + '/x11'
    FileUtils.mkdir_p([src, obj + '/gen', dest + '/usr/include', x11 + '/include'])
    File.write(src + '/probe.c', "#include <probe.h>\n#include <xprobe.h>\nconst char source_name[] = __FILE__;\nint reproducible_probe(int value) { return value + 1; }\n")
    File.write(obj + '/gen/generated.c', "const char object_name[] = __FILE__;\nint reproducible_generated(int value) { return value + 2; }\n")
    File.write(dest + '/usr/include/probe.h', "const char dest_name[] = __FILE__;\n")
    File.write(x11 + '/include/xprobe.h', "const char x11_name[] = __FILE__;\n")
    args = [make, '-C', obj, '-m', os + '/share/mk', '-f', fixture, 'MKREPRO=yes',
      'MKLLVM=no', 'NETBSDSRCDIR=' + src, 'NETBSDOBJDIR=' + obj,
      'DESTDIR=' + dest, 'X11SRCDIR=' + x11, 'EXTERNAL_TOOLCHAIN=' + prefix]
    out, err, status = Open3.capture3(*args, 'check')
    abort "External GCC reproducible compile failed:\n#{out}#{err}" unless status.success?
    %w[probe.o generated.o].each do |name|
      path = obj + '/' + name
      debug, err, status = Open3.capture3(readelf, '--debug-dump=info', '--debug-dump=decodedline', path)
      abort err unless status.success?
      abort 'Build root leaked into object bytes' if File.binread(path).include?(root)
      abort 'Object directory is not canonical' unless debug.include?('/usr/obj')
      expected = name == 'probe.o' ? '/usr/src/probe.c' : '/usr/obj/gen/generated.c'
      abort 'Source name is not canonical' unless debug.include?(expected)
      abort '__FILE__ is not canonical' unless File.binread(path).include?(expected + "\0")
      if name == 'probe.o'
        %w[/usr/include/probe.h /usr/xsrc/include/xprobe.h].each do |header|
          abort 'Header macro path is not canonical' unless File.binread(path).include?(header + "\0")
        end
      end
    end
    if ENV['CPPCHECK']
      out, err, status = Open3.capture3(ENV['CPPCHECK'], '--enable=warning,performance,portability', '--error-exitcode=1', '--std=c11', '--platform=unix64', '-D__NetBSD__', '-D__x86_64__', '-I' + dest + '/usr/include', '-I' + x11 + '/include', src + '/probe.c', obj + '/gen/generated.c')
      abort "Cppcheck fixture analysis failed: #{out}#{err}" unless status.success?
    end
    outputs << obj
    if index.zero?
      _, err, status = Open3.capture3(*args.reject { |arg| arg.start_with?('NETBSDOBJDIR=') }, 'NETBSDOBJDIR=', '-V', '${REPROFLAGS}')
      abort 'Missing external object root was accepted' if status.success? || !err.include?('NETBSDOBJDIR')
      raw_make = File.dirname(make) + '/nbmake'
      native_args = [raw_make, '-C', obj, '-m', os + '/share/mk', '-f', fixture,
        'MACHINE=amd64', 'MACHINE_ARCH=x86_64', 'TOOLDIR=' + File.dirname(File.dirname(make)),
        'USETOOLS=yes', 'MAKECONF=/dev/null', 'MKREPRO=yes', 'MKLLVM=no',
        'NETBSDSRCDIR=' + src, 'NETBSDOBJDIR=' + obj, 'DESTDIR=' + dest, 'X11SRCDIR=' + x11]
      flags, err, status = Open3.capture3({'EXTERNAL_TOOLCHAIN' => nil, 'MAKEFLAGS' => nil, 'MAKEOBJDIR' => nil}, *native_args, '-V', '${HAVE_GCC}', '-V', '${REPROFLAGS}', '-V', '${CPPFLAGS}')
      abort "Cannot inspect in-tree flags: #{err}" unless status.success?
      lines = flags.lines.map(&:strip)
      abort 'In-tree GCC12 regex mapping changed' unless lines[0] == '12' && lines[1].scan('-fdebug-regex-map=').size == 4
      expected_regex = [
        %q{-fdebug-regex-map='/usr/src/(.*)/obj$=/usr/obj/\1'},
        %q{-fdebug-regex-map='/usr/src/(.*)/obj/(.*)=/usr/obj/\1/\2'},
        %q{-fdebug-regex-map='/usr/src/(.*)/obj\..*=/usr/obj/\1'},
        %q{-fdebug-regex-map='/usr/src/(.*)/obj\..*/(.*)=/usr/obj/\1/\2'}
      ]
      abort 'An in-tree regex replacement changed' unless expected_regex.all? { |flag| lines[1].include?(flag) }
      abort 'In-tree environment maps changed' unless lines[1].include?('\$NETBSDSRCDIR=/usr/src') && lines[1].include?('\$NETBSDOBJDIR=/usr/obj')
      abort 'In-tree preprocessor maps changed' unless lines[2].include?('-Wp,-iremap,')
      puts 'PASS: in-tree GCC12 retains its environment, preprocessor and four regex maps'

    end
  end
  %w[probe.o generated.o].each do |name|
    abort "Different bytes for #{name}" unless File.binread(outputs[0] + '/' + name) == File.binread(outputs[1] + '/' + name)
    puts "PASS: distinct source/object roots produce identical #{name}: #{Digest::SHA256.file(outputs[0] + '/' + name).hexdigest}"
  end
end
puts 'PASS: real GCC16/nbmake MKREPRO objects, canonical DWARF and existing macro maps; no target execution'
