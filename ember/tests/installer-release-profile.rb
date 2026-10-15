# Origin: EmberBSD amd64 release profile contract, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'open3'
root = File.expand_path('../..', __dir__)
make = ARGV.fetch(0) { abort 'Usage: ruby installer-release-profile.rb host-nbmake' }
Dir.mktmpdir do |dir|
  File.write(File.join(dir, 'Makefile'), ".include \"#{root}/etc/etc.amd64/Makefile.inc\"\n")
  [{ 'KERNEL_SETS' => nil, 'BUILD_KERNELS' => nil },
   { 'KERNEL_SETS' => 'GENERIC', 'BUILD_KERNELS' => 'INSTALL' }].each_with_index do |env, n|
    out, err, st = Open3.capture3(env, make, '-V', 'KERNEL_SETS', '-V', 'BUILD_KERNELS', chdir: dir)
    abort err unless st.success?
    expected = n == 0 ? [ 'GENERIC GENERIC_KASLR XEN3_DOM0 XEN3_DOMU MICROVM', 'INSTALL INSTALL_XEN3_DOMU' ] : [ 'GENERIC', 'INSTALL' ]
    abort "FAIL: configured release profile ignored: #{out.inspect}" unless out.lines.map(&:strip) == expected
  end
end
puts 'PASS: amd64 default kernels and explicit single-kernel release profile'
