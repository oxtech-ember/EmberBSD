#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Run production allocator and snapshot code with modeled kernel services.
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source=ARGV[0] ? File.expand_path(ARGV[0]) : File.expand_path("../..", __dir__)
output=ARGV[1] && File.expand_path(ARGV[1])
work=output || Dir.mktmpdir('v3d-bo-stats-')
abort "output must be empty: #{work}" if output && File.exist?(work) && !Dir.empty?(work)
FileUtils.mkdir_p(work)
commands=[]
run=lambda do |label, command, success=true|
  text, status=Open3.capture2e(*command)
  File.write(File.join(work,label+'.log'),text)
  commands << {label:label,arguments:command,status:status.exitstatus,signal:status.termsig}
  File.write(File.join(work,'commands.json'),JSON.pretty_generate(commands)+"\n")
  abort "#{label} failed: #{text}" if status.success? != success
end
begin
  driver=source+'/sys/external/bsd/drm2/bcmv3d/'
  stripped=lambda {|s| s.lines.reject {|line| line.start_with?('#include ', '__KERNEL_RCSID(')}.join }
  bodies={
    'bo-body.h'=>stripped.call(File.read(driver+'bcmv3d_bo.c')),
    'drv-body.h'=>stripped.call(File.read(driver+'bcmv3d_drv.h')),
    'uapi-body.h'=>stripped.call(File.read(source+'/sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h'))
  }
  bodies['stats-body.h']=bodies['bo-body.h'][/\/\* All counters.*?(?=static void\nbcmv3d_bo_free)/m] or abort 'snapshot source boundary missing'
  bodies['sysctl-body.h']=File.read(driver+'bcmv3d_drv.c')[/\/\* Copy under.*?(?=\/\* A PRIME\b)/m] or abort 'sysctl source boundary missing'
  bodies.each {|name,data| File.write(File.join(work,name),data)}
  FileUtils.mkdir_p(File.join(work,'sys'))
  File.write(File.join(work,'sys','sysctl.h'), <<~HEADER)
    #ifndef EMBER_V3D_BO_STATS_TEST_SYSCTL_H
    #define EMBER_V3D_BO_STATS_TEST_SYSCTL_H
    #include <stddef.h>
    int sysctlbyname(const char *, void *, size_t *, void *, size_t);
    #endif
  HEADER
  flags=['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function',*Shellwords.split(ENV.fetch('CFLAGS','')),'-I'+work]
  cc=Shellwords.split(ENV.fetch('CC','cc'))
  %w[contract threads cli-test].each do |test|
    binary=File.join(work,test)
    run.call(test+'-compile',[*cc,*flags,'-pthread',source+"/ember/tools/bcm2712-v3d-bo-stats-#{test}.c",'-o',binary])
    run.call(test+'-run',[binary])
  end
  unless ENV['V3D_SKIP_MUTANTS']=='1'
    Process.setrlimit(Process::RLIMIT_CORE,0,0)
    mutants={
      'missing-dma-failure'=>['bo-body.h',/bcmv3d_bo_failure\(sc, BCMV3D_BO_STAGE_DMA_ALLOC, requested,\s*size, error\);/,'/* omitted diagnostic */'],
      'missing-final-release'=>['bo-body.h',/bcmv3d_bo_count\(&sc->bo_stats.releases\);/,'/* omitted release count */'],
      'missing-snapshot-lock'=>['bo-body.h',/mutex_enter\(&sc->memory_lock\);\n\t\*snapshot = sc->bo_stats;/,'*snapshot = sc->bo_stats;'],
      'writable-snapshot'=>['sysctl-body.h',/if \(newp != NULL\)\n\t\treturn EPERM;/,'/* write guard omitted */'],
      'missing-teardown'=>['sysctl-body.h',/sysctl_teardown\(&sc->sysctllog\);/,'/* teardown omitted */']
    }
    mutants.each do |name,(file,pattern,replacement)|
      bodies.each {|path,data| File.write(File.join(work,path),data)}
      text=bodies.fetch(file)
      abort "mutation missing: #{name}" unless text.match?(pattern)
      File.write(File.join(work,file),text.sub(pattern,replacement))
      binary=File.join(work,'mutant')
      run.call(name+'-compile', [*cc,*flags,source+'/ember/tools/bcm2712-v3d-bo-stats-contract.c','-o',binary])
      run.call(name+'-run',[binary],false)
    end
    bodies.each {|path,data| File.write(File.join(work,path),data)}
  end
  puts 'PASS actual-source BO accounting, sysctl, concurrent snapshots, CLI and causal controls'
ensure
  FileUtils.remove_entry(work) unless output
end
