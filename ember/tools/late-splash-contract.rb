#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Actual simplefb/genfb attachment bodies; kernel services are controlled.
require 'tmpdir'
require 'fileutils'
require 'shellwords'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
def function(text, name)
  matches = text.scan(/^(?:static )?(?:int|void)\n#{Regexp.escape(name)}\(.*?^}/m)
  abort "function count #{name}: #{matches.size}" unless matches.size == 1
  matches.first
end
Dir.mktmpdir('late-splash-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  %w[sys/dev/fdt/simplefb.c sys/dev/wsfb/genfb.c].each do |path|
    text = File.read(source + '/' + path)
    names = path.include?('simplefb') ? %w[simplefb_attach_genfb] : %w[genfb_init_screen genfb_attach]
    license = text.split(/^#(?:include|ifdef)/, 2).first
    File.write(work + '/' + File.basename(path) + '.h', license + names.map { |n| function(text, n) }.join("\n"))
  end
  File.write(work + '/splash-mask.h', File.read(source + '/sys/dev/splash/splash.h').lines.drop_while { |l| !l.start_with?('#define SPLASH_BOOT_DISABLE_MASK') }.take(2).join)
  cc = Shellwords.split(ENV.fetch('CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
  [[true, true, false], [true, false, false], [false, true, false], [true, true, true]].each do |splash, preserve, multi|
    defs = []
    defs << '-DSPLASHSCREEN' if splash
    defs << '-DWSDISPLAY_EARLYFB_PRESERVE_SPLASH' if preserve
    defs << '-DWSDISPLAY_MULTICONS' if multi
    argv = [*cc, '-std=c11', '-Wall', '-Werror', '-Wno-unused-function', *flags, *defs, '-I' + work,
            source + '/ember/tools/late-splash-contract.c', '-o', work + '/contract']
    puts Shellwords.join(argv)
    abort 'compile failed' unless system(*argv)
    abort 'late splash contract failed' unless system(work + '/contract')
    if splash && preserve && !multi && ENV['LATE_SPLASH_ANALYZE'] == '1'
      analyzer = argv[0...-2] + ['--analyze', '-Xanalyzer', '-analyzer-output=text']
      abort 'Clang analysis failed' unless system(*analyzer)
      cppcheck = ENV.fetch('CPPCHECK', 'cppcheck')
      abort 'Cppcheck failed' unless system(cppcheck, '--enable=warning,performance,portability',
        '--error-exitcode=1', '--inline-suppr', '--check-level=exhaustive', *defs, '-I' + work,
        source + '/ember/tools/late-splash-contract.c')
    end
  end
end
