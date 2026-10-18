#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted pinned TF-A host regression.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'

abort 'usage: firmware-tfa-host.rb PINNED_UTILITIES_MK EMPTY_OUTPUT' unless ARGV.length == 2
source, output = ARGV.map { |path| File.expand_path(path) }
root = File.expand_path('../..', __dir__)
patch = root+'/ember/firmware/tfa-shell-slice.diff'
abort 'Output must be empty' if File.exist?(output) && !Dir.empty?(output)
original = File.binread(source)
source_sha = Digest::SHA256.hexdigest(original)
abort 'Expected the pinned TF-A utilities.mk' unless source_sha == 'b33645bea59f264614c6c04a82ac619747d00fa8b1c58f3f093a41b8e916d6f5'
FileUtils.mkdir_p(output+'/patched/make_helpers')
File.binwrite(output+'/baseline.mk',original)
File.binwrite(output+'/patched/make_helpers/utilities.mk',original)
commands = []
run = lambda do |name, argv, input|
  environment = {'PATH'=>'/usr/bin:/bin:/usr/sbin:/sbin'}
  stdout, stderr, status = Open3.capture3(environment,*argv,stdin_data:input)
  commands << {name:name,argv:argv,environment:environment,stdin:input,
               status:status.exitstatus,stdout:stdout,stderr:stderr}
  File.write(output+'/commands.json',JSON.pretty_generate(commands)+"\n")
  [stdout,stderr,status]
end
_, err, status = run.call('apply-TFA-only-patch',
  ['patch','-s','-p1','-F0','-d',output+'/patched'],File.read(patch))
abort 'Patch failed: '+err unless status.success?
cases = [
  ["alpha 'beta gamma' delta",nil,nil,%w[alpha]+['beta gamma','delta']],
  ["alpha 'beta gamma' delta",1,3,['alpha','beta gamma']],
  ["alpha 'beta gamma' delta",2,nil,['beta gamma','delta']],
  ["alpha 'beta gamma' delta",2,4,['beta gamma','delta']],
  ["alpha 'beta gamma' delta",2,5,['beta gamma','delta']],
  ['alpha',2,nil,[]],
  [%q{alpha "two words" "c'd" ""},2,5,['two words',"c'd",'']]
]
makefile=cases.each_with_index.map do |(words,first,last,_),index|
  "$(info CASE_#{index}=$(call shell-slice,#{words},#{first},#{last}))\n"
end.join+".PHONY: firmware-host\nfirmware-host:\n\t@:\n"
make=ENV.fetch('FIRMWARE_TEST_GMAKE','gmake')
unless make.start_with?('/')
  make=ENV.fetch('PATH').split(File::PATH_SEPARATOR).map { |path| path+'/'+make }
    .find { |path| File.file?(path) && File.executable?(path) }
end
abort 'GNU Make is required' unless make && File.executable?(make)
profile=root+'/ember/firmware/gcc-profile.sh'
call_helper=lambda do |name,file|
  run.call(name,['/bin/bash','-c','source "$1"; shift; firmware_gcc_query "$@"',
    'tfa-host',profile,make,'-f',file,'-f','-','-n','firmware-host'],makefile)
end
out,err,status=call_helper.call('baseline-upstream-helper',output+'/baseline.mk')
abort 'Baseline did not expose the stock BSD sed failure' unless status.success? &&
  err.include?('extra characters at the end of p command') && !out.include?("'beta gamma'")
puts 'PASS: baseline upstream helper exposes BSD sed failure despite make status 0'
out,err,status=call_helper.call('patched-upstream-helper',output+'/patched/make_helpers/utilities.mk')
abort 'Patched helper failed: '+err unless status.success? && err.empty?
cases.each_with_index do |(_,_,_,expected),index|
  line=out.lines.find { |entry| entry.start_with?("CASE_#{index}=") }
  abort "Missing case #{index}" unless line
  actual=Shellwords.split(line.strip.delete_prefix("CASE_#{index}="))
  abort "Case #{index}: expected #{expected.inspect}, got #{actual.inspect}" unless actual==expected
  puts "PASS: upstream shell-slice case #{index}"
end
abort 'Input source changed' unless Digest::SHA256.file(source).hexdigest==source_sha
File.write(output+'/receipt.json',JSON.pretty_generate({
  upstream_revision:'a5c580fd539929af41cf483b7c1434db261dd51b',
  upstream_url:'https://github.com/ARM-software/arm-trusted-firmware/blob/a5c580fd539929af41cf483b7c1434db261dd51b/make_helpers/utilities.mk#L521',
  upstream_sha256:source_sha,patch_sha256:Digest::SHA256.file(patch).hexdigest,
  patched_sha256:Digest::SHA256.file(output+'/patched/make_helpers/utilities.mk').hexdigest,
  cases:cases.length,sed:'/usr/bin/sed',scope:'Real pinned GNU Make helper with stock macOS BSD sed. No target compiler, firmware build, network or device access.'})+"\n")
puts "PASS: #{cases.length} pinned TF-A helper cases and causal baseline"
