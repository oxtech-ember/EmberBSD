# Origin: EmberBSD X11 font-tool host compilation regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'shellwords'
require 'tmpdir'

abort 'Usage: x11-host-freetype.rb nbmake-amd64 xsrc-directory' unless ARGV.size == 2
make, xsrc = ARGV.map { |arg| File.expand_path(arg) }
root = File.expand_path('../..', __dir__)
gzip = File.join(xsrc, 'external/mit/freetype/dist/src/gzip/ftgzip.c')
abort 'Missing FreeType source' unless File.file?(gzip)
# The real Makefiles must compile their gzip component on the host; removing
# their system-zlib selection reproduces the missing Byte type on macOS.
Dir.mktmpdir('ember-x11-host') do |work|
  %w[fc-cache mkfontscale].each do |tool|
    dir = File.join(root, 'external/mit/xorg/tools', tool)
    flags, status = Open3.capture2(make, '-C', dir, "X11SRCDIR=#{xsrc}", '-v', 'HOST_CPPFLAGS')
    abort "Cannot evaluate #{tool} flags" unless status.success?
    compiler = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
    output, status = Open3.capture2e(*compiler, '-std=gnu99', *Shellwords.split(flags),
      '-c', gzip, '-o', File.join(work, tool + '.o'))
    abort "#{tool} FreeType host compilation failed:\n#{output}" unless status.success?
    puts "#{tool}: FreeType gzip host compilation passes"
  end
end
