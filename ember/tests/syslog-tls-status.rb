#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted), actual syslogd status format regression.
require 'digest'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'tmpdir'
abort 'Usage: syslog-tls-status.rb SOURCE_ROOT NEW_RECEIPT_DIR [TLS_SOURCE]' unless [2,3].include?(ARGV.size)
root,receipt=ARGV.first(2).map { |p| File.expand_path(p) }
source=ARGV[2] ? File.expand_path(ARGV[2]) : File.join(root,'usr.sbin/syslogd/tls.c')
header=File.join(root,'usr.sbin/syslogd/syslogd.h')
abort 'Receipt path already exists' if File.exist?(receipt)
cc=ENV.fetch('CC','cc'); analyzer=ENV.fetch('ANALYZER_CC',cc)
cppcheck=ENV.fetch('CPPCHECK','cppcheck')
text=File.binread(source)
format=text[/\tsnprintf\(statusmsg, sizeof\(statusmsg\),\n.*?\tFREEPTR\(fp\);\n/m] or abort 'Cannot extract actual status format and cleanup'
macro=File.binread(header)[/^#define FREEPTR\(x\).*?(?=^#define FREE_SSL)/m] or abort 'Cannot extract actual FREEPTR'
FileUtils.mkdir_p(receipt)
def run(argv,log,chdir:nil)
  out,status=Open3.capture2e(*argv,**(chdir ? {chdir:chdir} : {}))
  File.write(log,"$ #{argv.shelljoin}\n#{out}\nexit=#{status.exitstatus}; signal=#{status.termsig}\n")
  [out,status]
end
run([cc,'--version'],File.join(receipt,'compiler-version.log'))
run([analyzer,'--version'],File.join(receipt,'clang-version.log'))
run([cppcheck,'--version'],File.join(receipt,'cppcheck-version.log'))
flags=['-std=c11','-O0','-Wall','-Wextra','-Werror',*Shellwords.split(ENV.fetch('HOST_CFLAGS',''))]
results=[]
Dir.mktmpdir('syslog-tls-status-') do |work|
  File.write(File.join(receipt,'temporary-path.txt'),"#{work}\n")
  FileUtils.cp(File.join(__dir__,'syslog-tls-status.c'),File.join(work,'fixture.c'))
  File.write(File.join(work,'freeptr.h'),macro)
  File.write(File.join(work,'status-format.c'),format)
  File.write(File.join(receipt,'status-format.c'),format)
  File.write(File.join(receipt,'freeptr.h'),macro)
  _,build=run([cc,*flags,'fixture.c','-o','fixture'],File.join(receipt,'compile.log'),chdir:work)
  abort 'Host format fixture compilation failed' unless build.success?
  out,status=run([File.join(work,'fixture')],File.join(receipt,'run.log'))
  puts out
  results << status.success?
  _,build=run([cc,*flags,'-fsanitize=address,undefined','-fno-omit-frame-pointer','fixture.c','-o','fixture-asan'],File.join(receipt,'asan-compile.log'),chdir:work)
  abort 'Host sanitizer compilation failed' unless build.success?
  _,status=run([File.join(work,'fixture-asan')],File.join(receipt,'asan-run.log'))
  results << status.success?
  _,analysis=run([analyzer,*flags.reject { |f| f=='-Werror' },'--analyze','-Xanalyzer','-analyzer-output=text','fixture.c'],File.join(receipt,'clang-analyzer.log'),chdir:work)
  abort 'Clang Static Analyzer failed' unless analysis.success?
  _,analysis=run([cppcheck,'--quiet','--language=c','--enable=warning,style,performance,portability','--std=c11','--platform=unix64','--template={file}:{line}:{severity}:{id}:{message}','fixture.c'],File.join(receipt,'cppcheck.log'),chdir:work)
  abort 'Cppcheck failed' unless analysis.success?
end
files=[source,header,__FILE__,File.join(__dir__,'syslog-tls-status.c')]
File.write(File.join(receipt,'inputs.sha256'),files.map { |p| "#{Digest::SHA256.file(p).hexdigest}  #{p}\n" }.join)
File.write(File.join(receipt,'scope.txt'),"Actual production status-format statements and FREEPTR macro.\nHost format/ownership regression only, not a TLS handshake or trust-policy test.\nTemporary source and executables were removed.\n")
abort 'Status format or ownership regression failed' unless results.all?
puts 'PASS: actual TLS status format and allocation cleanup'
