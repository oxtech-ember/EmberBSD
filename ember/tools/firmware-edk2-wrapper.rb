#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted actual EDK2 shell-wrapper regression.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'rbconfig'

abort 'usage: firmware-edk2-wrapper.rb PINNED_EDK2_SOURCE EMPTY_OUTPUT [BASH...]' unless ARGV.length>=2
source,output=ARGV.shift(2).map { |path| File.expand_path(path) }
source=File.realpath(source)
abort 'Output must not be a symlink' if File.symlink?(output)
output=File.join(File.realpath(File.dirname(output)),File.basename(output))
abort 'Source and output must not overlap' if output==source ||
  output.start_with?(source+'/') || source.start_with?(output+'/')
shells=ARGV.empty? ? ['/bin/bash'] : ARGV
root=File.expand_path('../..',__dir__)
revision='7bbf1f2022f381f48cfb5411df9e5157ee0291a5'
abort 'Output must be empty' if File.exist?(output) && (!File.directory?(output) || !Dir.empty?(output))
FileUtils.mkdir_p(output)
commands=[]
run=lambda do |name,argv,env={},input=nil|
  stdout,stderr,status=Open3.capture3(env,*argv,stdin_data:input.to_s)
  commands << {name:name,argv:argv,environment:env,status:status.exitstatus,stdout:stdout,stderr:stderr}
  File.write(output+'/commands.json',JSON.pretty_generate(commands)+"\n")
  [stdout,stderr,status]
end
git=lambda do |*args|
  argv=['git','--no-optional-locks','-c','protocol.allow=never','-C',source,*args]
  stdout,stderr,status=run.call('source-'+args.first,argv,{'GIT_NO_LAZY_FETCH'=>'1'})
  abort stderr unless status.success?
  stdout
end
abort 'Wrong EDK2 source pin' unless git.call('rev-parse','HEAD').strip==revision
original={}
%w[GenFw GenericShellWrapper].each do |name|
  relative='BaseTools/BinWrappers/PosixLike/'+name
  original[name]=git.call('show',revision+':'+relative)
  %w[baseline patched].each do |version|
    directory=output+'/'+version+'/BaseTools/BinWrappers/PosixLike'
    FileUtils.mkdir_p(directory)
    File.binwrite(directory+'/'+name,original.fetch(name))
  end
end
patch=root+'/ember/firmware/edk2-shell-wrapper.diff'
_,err,status=run.call('apply-EDK2-patch',['patch','-s','-t','-N','-p1','-F0','-d',output+'/patched'],{},File.binread(patch))
abort err unless status.success?
arguments=['one','two words','',%q{quote'"\slash},'--last']
results=[]
shells.each_with_index do |shell,index|
  version,err,status=run.call('bash-version-'+index.to_s,[shell,'--version'])
  abort err unless status.success?
  legacy=nil
  %w[direct source-c outermost].each do |mode|
    %w[baseline patched].each do |variant|
      fixture=output+'/'+variant
      wrappers=fixture+'/BaseTools/BinWrappers/PosixLike'
      bins=fixture+'/BaseTools/Source/C/bin'
      FileUtils.mkdir_p(bins)
      backend=<<~CODE
        #!#{RbConfig.ruby}
        require 'json'
        File.write(ENV.fetch('WRAPPER_CAPTURE'),JSON.generate({tool:File.basename($0),argv:ARGV}))
      CODE
      %w[GenFw OuterTool].each do |name|
        File.write(bins+'/'+name,backend);File.chmod(0o700,bins+'/'+name)
      end
      outer=wrappers+'/OuterTool'
      File.write(outer,"target=$1; shift; . \"$target\"\n")
      capture=output+"/capture-#{index}-#{mode}-#{variant}.json"
      env={'PATH'=>wrappers+':/usr/bin:/bin','WRAPPER_CAPTURE'=>capture,
           'EDK_TOOLS_PATH'=>mode=='outermost' ? '' : fixture+'/BaseTools'}
      argv=case mode
      when 'direct' then [shell,wrappers+'/GenFw',*arguments]
      when 'source-c' then [shell,'-c','target=$1; shift; . "$target"','not-the-tool',wrappers+'/GenFw',*arguments]
      else [shell,outer,wrappers+'/GenFw',*arguments]
      end
      _,stderr,status=run.call("#{index}-#{mode}-#{variant}",argv,env)
      legacy=stderr.include?('bad array subscript') if mode=='direct' && variant=='baseline'
      if variant=='baseline' && mode=='source-c' && legacy
        abort 'Expected the causal sourced-wrapper failure' unless status.success? && !File.exist?(capture)
      else
        abort 'Wrapper failed: '+stderr unless status.success?
        actual=JSON.parse(File.read(capture))
        expected={'tool'=>mode=='outermost' ? 'OuterTool' : 'GenFw','argv'=>arguments}
        abort 'Wrapper changed script identity or argv' unless actual==expected
      end
      abort 'Patched wrapper emitted a diagnostic: '+stderr if variant=='patched' && !stderr.empty?
      abort 'Unexpected baseline diagnostic: '+stderr if variant=='baseline' && !stderr.empty? && !stderr.include?('bad array subscript')
      results << {bash:shell,mode:mode,variant:variant,legacy_subscript:legacy,status:status.exitstatus,backend_ran:File.exist?(capture)}
    end
    puts "PASS: #{shell} #{mode} script identity and argv"
  end
end
File.write(output+'/receipt.json',JSON.pretty_generate({revision:revision,
  original_sha256:original.to_h { |name,bytes| [name,Digest::SHA256.hexdigest(bytes)] },
  patch_sha256:Digest::SHA256.file(patch).hexdigest,
  patched_sha256:Digest::SHA256.file(output+'/patched/BaseTools/BinWrappers/PosixLike/GenericShellWrapper').hexdigest,
  results:results,scope:'Unmodified/patched pinned wrappers with an explicit argv-capture backend; no compiler, real firmware build or device access.'})+"\n")
puts "PASS: #{shells.length} Bash versions, 3 invocation modes, old/new controls"
