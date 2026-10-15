#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD tests of the compiled sysinst download defaults.
require 'open3'
require 'tmpdir'

root = File.expand_path('../..', __dir__)
defs = File.read(File.join(root, 'usr.sbin/sysinst/defs.h'))
block = defs[/\/\* OS sets come from EmberBSD\..*?(?=#if !defined\(PKG_ARCH_SUBDIR\))/m]
abort 'Cannot find the OS download defaults' unless block
accepted = 'oxtech-ember/EmberBSD/releases/download/' \
  'build-a0674c0d2c1b067339b9436cf6148426546649ef-amd64-no-x11'
cases = [
  ['amd64 accepted source', %w[-DMACH_amd64 -DMACH="amd64"],
   ['github.com', 'github.com', accepted, '']],
  ['aarch64 keeps its own layout', %w[-DMACH_evbarm -DMACH="evbarm" -DARCH_SUBDIR="aarch64"],
   ['cdn.emberbsd.oxteam.me', 'cdn.emberbsd.oxteam.me', 'releases/development', '/aarch64/binary/sets']],
  ['pinned release overrides', %w[-DMACH_amd64 -DMACH="amd64" -DSYSINST_HTTP_HOST="mirror.example" -DSYSINST_FTP_HOST="mirror.example" -DSYSINST_FTP_DIR="fixed/second" -DSYSINST_FTP_SET_DIR_BIN=""],
   ['mirror.example', 'mirror.example', 'fixed/second', '']]
]
Dir.mktmpdir('ember-sysinst-defaults-') do |dir|
  source = File.join(dir, 'defaults.c')
  File.write(source, "#include <stdio.h>\n#{block}\n" + <<~C)
    int main(void) {
      puts(SYSINST_HTTP_HOST);
      puts(SYSINST_FTP_HOST);
      puts(SYSINST_FTP_DIR);
      puts(SYSINST_FTP_SET_DIR_BIN);
      return 0;
    }
  C
  cases.each do |name, flags, expected|
    exe = File.join(dir, 'defaults')
    output, status = Open3.capture2e(ENV.fetch('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', *flags, source, '-o', exe)
    abort output unless status.success?
    output, status = Open3.capture2e(exe)
    abort "#{name}: #{output.inspect} != #{expected.inspect}" unless status.success? && output.split("\n", -1)[0...-1] == expected
    puts "PASS: #{name}"
  end
end
abort 'Local media prefix must remain architecture-specific' unless File.read(File.join(root, 'usr.sbin/sysinst/main.c')).include?('{"local prefix", "/" ARCH_SUBDIR "/binary/sets"')
puts 'PASS: local media prefix remains independent'
