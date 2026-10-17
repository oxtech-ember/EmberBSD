#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'fileutils'
require 'open3'
source = File.expand_path('cix-p1-root.subr', __dir__)
abort 'missing root startup library' unless File.file?(source)
checks = 0
Dir.mktmpdir('cix-root-') do |tmp|
  %w[fsck mount mount_tmpfs reboot].each do |command|
    path = File.join(tmp, command)
    File.write(path, <<~SH)
      #!/bin/sh
      echo '#{command}'" $*" >> "$TRACE"
      [ '#{command}' != fsck ] || exit "$FSCK_STATUS"
      [ '#{command}' != "$FAIL" ] || exit 9
      exit 0
    SH
    FileUtils.chmod(0755, path)
  end
  trace = File.join(tmp, 'trace')
  run = lambda do |status, failure = ''|
    File.write(trace, '')
    output, result = Open3.capture2e({'PATH' => "#{tmp}:/usr/bin:/bin", 'TRACE' => trace,
                                    'FSCK_STATUS' => status.to_s, 'FAIL' => failure},
                                   'sh', '-c', '. "$1"; cix_root_start', 'contract', source)
    [result.success?, File.readlines(trace, chomp: true), output]
  end
  check = lambda do |condition, message|
    checks += 1
    abort "FAIL: #{message}" unless condition
  end
  ok, lines, = run.call(0)
  check.call(ok, 'clean root starts')
  check.call(lines == ['fsck -p /', 'mount -u -w /', 'mount_tmpfs -s 32M tmpfs /var/run',
                       'mount_tmpfs -s 128M tmpfs /tmp'], 'check root before writable mount and isolate runtime paths')
  [2, 8, 12, 99].each do |code|
    ok, lines, = run.call(code)
    check.call(!ok && lines == ['fsck -p /'], "fsck #{code} returns to single user without writes")
  end
  ok, lines, = run.call(4)
  check.call(!ok && lines == ['fsck -p /', 'reboot -n'], 'modified mounted root reboots without sync')
  ok, lines, = run.call(0, 'mount')
  check.call(!ok && lines.size == 2, 'writable mount failure stops boot')
  ok, lines, = run.call(0, 'mount_tmpfs')
  check.call(!ok && lines.size == 3, 'runtime tmpfs failure stops boot')
end
puts "CIX root startup: #{checks} checks, 0 failures"
