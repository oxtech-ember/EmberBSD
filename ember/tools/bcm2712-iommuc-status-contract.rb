#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'json'
require 'digest'
root=File.realpath(ARGV[0] || File.expand_path('../..',__dir__))
evidence=ENV['ICAC_STATUS_EVIDENCE'];abort 'new evidence required' if evidence && File.exist?(evidence)
FileUtils.mkdir_p(evidence) if evidence
commands=[]
run=lambda do |name,args|
 o,e,s=Open3.capture3(*args)
 if evidence
  File.write(evidence+'/'+name+'.stdout',o);File.write(evidence+'/'+name+'.stderr',e)
 end
 commands << {name:name,argv:args,status:s.exitstatus,signal:s.termsig}
 File.write(evidence+'/commands.json',JSON.pretty_generate(commands)+"\n") if evidence
 print o;warn e unless e.empty?;[s,e]
end
files={}
Dir.mktmpdir('bcm2712-iommuc-status-',ENV.fetch('TMPDIR','/tmp')) do |work|
 begin
  puts 'cached ICAC status temporary directory: '+work
  {'iommuc.h'=>'sys/arch/arm/broadcom/bcm2712_iommuc.h',
   'core.h'=>'sys/arch/arm/broadcom/bcm2712_iommuc_corevar.h',
   'native.h'=>'sys/arch/arm/broadcom/bcm2712_iommuc_native.h',
   'module.h'=>'sys/modules/bcm2712_iommuc_status/bcm2712_iommuc_status.c'}.each do |name,path|
   t=File.binread(root+'/'+path);files[path]=Digest::SHA256.hexdigest(t)
   File.write(work+'/'+name,t.gsub(/^#include .*\n|^__KERNEL_RCSID\(.*\n/,''))
  end
  %w[ember/tools/bcm2712-iommuc-status-contract.c ember/tools/bcm2712-iommuc-status-contract.rb sys/modules/bcm2712_iommuc_status/Makefile].each{|p|files[p]=Digest::SHA256.file(root+'/'+p).hexdigest}
  module_text=File.read(work+'/module.h')
  abort 'unexpected hardware path' if module_text.match?(/observe_control|bus_space_|bcmmbox|clock_|kmem_|static\s+struct/)
  cc=Shellwords.split(ENV.fetch('CC','cc'));flags=Shellwords.split(ENV.fetch('CFLAGS',''))
  compile=lambda do |name|
   s,_=run.call('compile-'+name,[*cc,'-std=c11','-Wall','-Wextra','-Werror',*flags,'-I'+work,'-I'+root+'/sys/external/bsd/acpica/dist/include',root+'/ember/tools/bcm2712-iommuc-status-contract.c','-o',work+'/contract'])
   abort 'compile failed' unless s.success?
  end
  compile.call('positive');s,_=run.call('positive',[work+'/contract']);abort 'positive failed' unless s.success?
  unless ENV['ICAC_STATUS_SKIP_MUTANTS']=='1'
   controls={
    'handle-error'=>['if (ACPI_FAILURE(AcpiGetHandle(NULL, BCM2712_IOMMUC_PATH, &handle)))','if (AcpiGetHandle(NULL, BCM2712_IOMMUC_PATH, &handle) == AE_OK && false)','mode != 1'],
    'lookup-error'=>["error = bcm2712_iommuc_lookup(handle, &provider);\n\tif (error != 0)","error = bcm2712_iommuc_lookup(handle, &provider);\n\tif (false)",'mode != 2'],
    'status-error'=>["error = bcm2712_iommuc_cached_status(provider, &status);\n\tif (error != 0)","error = bcm2712_iommuc_cached_status(provider, &status);\n\tif (false)",'mode != 3'],
    'output-zero'=>['memset(&status, 0, sizeof(status));','memset(&status, 0xa5, sizeof(status));','memcmp(out,&zero,sizeof(zero)) == 0'],
    'provider-zero'=>['*provider = NULL;','*provider = (void *)1;','*out == NULL'],
    'raw-output'=>['status.observation.raw_control, status.observation.first_error,','status.observation.raw_valid, status.observation.first_error,','strcmp(message,expected) == 0'],
    'fini'=>["if (command == MODULE_CMD_FINI)\n\t\treturn 0;","if (command == MODULE_CMD_FINI)\n\t\treturn EIO;",'MODULE_CMD_FINI,&provider_storage) == 0'],
    'no-observe'=>['error = bcm2712_iommuc_cached_status(provider, &status);',"(void)bcm2712_iommuc_observe_control(provider);\n\terror = bcm2712_iommuc_cached_status(provider, &status);",'forbidden == 0']
   }
   controls.each do |name,(before,after,expected)|
    abort 'anchor '+name unless module_text.include?(before)
    File.write(work+'/module.h',module_text.sub(before,after));compile.call(name)
    s,e=run.call('control-'+name,[work+'/contract'])
    abort 'cause not detected '+name unless s.termsig==Signal.list.fetch('ABRT') && e.include?('CHECK') && e.include?(expected)
    puts 'cached status control '+name+': CHECK'
   end
   File.write(work+'/module.h',module_text)
  end
 ensure
  if evidence
   FileUtils.cp_r(work,evidence+'/inputs')
   File.write(evidence+'/source.json',JSON.pretty_generate({sha256:files,extraction:'Complete actual headers/module body. Only include/RCS lines removed; source notices retained. Real ACPICA contract types. Controlled AcpiGetHandle/lookup/cached-status/log services; no actual hardware or provider implementation.',scope:'No fake positive hardware state; diagnostic reports controlled cached bytes only. Target module ABI/link and actual cached observation remain separate.'})+"\n")
  end
 end
end
