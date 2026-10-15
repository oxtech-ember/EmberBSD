# SPDX-License-Identifier: BSD-2-Clause
# Execute the real shell helper with pkgtools boundary doubles.
require 'tmpdir'
require 'fileutils'
require 'digest'
require 'open3'
source = ARGV[0] || File.expand_path('../..', __dir__) + '/libexec/ember-desktop/ember-runtime-install'
Dir.mktmpdir('ember-altroot-') do |d|
  d = File.realpath(d)
  root = d + '/root'
  cache = root + '/var/cache/ember-runtime'
  FileUtils.mkdir_p(cache + '/All')
  FileUtils.mkdir_p(root + '/usr/pkg')
  package = 'gcc16-runtime-16.2.0nb1'
  File.write(d + '/pkg_add', <<~SH)
    #!/bin/sh
    [ "$PKG_DBDIR" = '#{root}/usr/pkg/pkgdb' ] || exit 42
    [ -z "${LD_LIBRARY_PATH-}${LD_PRELOAD-}" ] || exit 43
    printf '%s\\n' "$@" > '#{d}/arguments'
    [ "${ADD_FAIL:-0}" = 0 ] || exit 17
    touch '#{d}/installed'
  SH
  File.write(d + '/pkg_info', <<~SH)
    #!/bin/sh
    case "$2" in
    gcc16-runtime-*) [ -f '#{d}/installed' ] && [ "${VERIFY_FAIL:-0}" = 0 ];;
    *) [ "${CONFLICT:-0}" = 1 ];;
    esac
  SH
  File.write(d + '/sha512', "#!/bin/sh\nshasum -a 512 \"$2\" | cut -d ' ' -f1\n")
  %w[pkg_add pkg_info sha512].each { |n| File.chmod(0755, d + '/' + n) }
  script = File.read(source).sub('$(id -u)', '0')
  %w[pkg_add pkg_info sha512].each do |n|
    script = script.gsub('/usr/sbin/' + n, d + '/' + n).gsub('/usr/bin/' + n, d + '/' + n)
  end
  helper = d + '/helper'
  File.write(helper, ENV['TRACE'] ? script.sub('set -eu', 'set -eux') : script)
  build = lambda do |extra = '', entry = nil|
    FileUtils.rm_rf(d + '/payload')
    FileUtils.mkdir_p(d + '/payload/gcc16/lib')
    File.write(d + '/payload/gcc16/lib/libgcc_s.so.1', 'fixture DSO')
    File.write(d + '/payload/+CONTENTS', "@cwd /usr/pkg\n@name #{package}\ngcc16/lib/libgcc_s.so.1\n" + extra)
    File.write(d + '/payload/' + entry, 'unexpected metadata') if entry
    out, err, st = Open3.capture3('tar', '-czf', cache + "/All/#{package}.tgz", '-C', d + '/payload', '+CONTENTS', 'gcc16/lib/libgcc_s.so.1', *[entry].compact)
    abort out + err unless st.success?
    File.write(cache + '/PROFILE', "gcc16\n")
    File.write(cache + '/ROOTS', package + "\n")
    sum = Digest::SHA512.file(cache + "/All/#{package}.tgz").hexdigest
    File.write(cache + '/SHA512', "SHA512 (#{package}.tgz) = #{sum}\n")
    FileUtils.rm_f([d + '/arguments', d + '/installed'])
  end
  run = ->(args = ['--root', root], env = {}) { Open3.capture3(env, 'sh', helper, *args) }
  reject = lambda do |label, args = ['--root', root], env = {}|
    out, err, st = run.call(args, env)
    abort "Accepted #{label}: #{out}#{err}" if st.success? || File.exist?(d + '/arguments')
  end
  build.call
  reject.call('unknown option', ['--unknown', root])
  reject.call('live root', ['--root', '/'])
  reject.call('relative root', ['--root', 'root'])
  reject.call('extra argument', ['--root', root, 'extra'])
  File.symlink(root, d + '/alias')
  reject.call('symlink root', ['--root', d + '/alias'])
  File.symlink(d, root + '/usr/pkg/gcc16')
  reject.call('symlink output directory')
  File.unlink(root + '/usr/pkg/gcc16')
  ['@exec touch /sentinel', '@unexec touch /sentinel', '@pkgdep foreign>=1', '@cwd /foreign', '@name foreign'].each do |line|
    build.call(line + "\n")
    reject.call(line)
  end
  ['+INSTALL', '+DEINSTALL', 'unexpected-file'].each do |entry|
    build.call('', entry)
    reject.call(entry)
  end
  build.call
  reject.call('provider conflict', ['--root', root], { 'CONFLICT' => '1' })
  out, err, st = run.call(['--root', root], { 'ADD_FAIL' => '1' })
  abort 'Accepted pkg_add failure' if st.success?
  FileUtils.rm_f(d + '/arguments')
  out, err, st = run.call(['--root', root], { 'VERIFY_FAIL' => '1' })
  abort 'Accepted post-install verification failure' if st.success?
  build.call
  out, err, st = run.call(['--root', root], { 'LD_LIBRARY_PATH' => '/foreign', 'LD_PRELOAD' => '/foreign.so' })
  abort out + err unless st.success?
  expected = ['-I', '-P', root, '-p', '/usr/pkg', '-K', '/usr/pkg/pkgdb', cache + "/All/#{package}.tgz"]
  abort 'Wrong alternate-root pkgtools arguments' unless File.readlines(d + '/arguments', chomp: true) == expected
end
puts 'PASS: alternate-root arguments, canonical paths, scriptless metadata, conflicts, failures and loader overrides'
