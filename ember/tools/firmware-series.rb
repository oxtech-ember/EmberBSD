#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted pinned firmware series check.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'

abort 'usage: firmware-series.rb SOURCE EMPTY_OUTPUT' unless ARGV.length == 2
source = File.realpath(ARGV[0])
output = File.expand_path(ARGV[1])
abort 'Output must not be a symlink' if File.symlink?(output)
output = File.join(File.realpath(File.dirname(output)),File.basename(output))
abort 'Source and output must not overlap' if output==source ||
  output.start_with?(source+'/') || source.start_with?(output+'/')
abort 'Output must be empty' if File.exist?(output) && (!File.directory?(output) || !Dir.empty?(output))
root = File.expand_path('../..',__dir__)
firmware = root+'/ember/firmware'
series = File.readlines(firmware+'/series',chomp:true)
abort 'Series must name each diff once' unless series.uniq==series &&
  series.all? { |name| name.match?(/\A[a-z0-9][a-z0-9-]*\.diff\z/) } &&
  series.sort==Dir[firmware+'/*.diff'].map { |path| File.basename(path) }.sort
pins = {'.'=>'246e70acd939d62b5a79f895984afd0a93bd83e9',
        'edk2-platforms'=>'c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed',
        'arm-trusted-firmware'=>'a5c580fd539929af41cf483b7c1434db261dd51b',
        'edk2'=>'7bbf1f2022f381f48cfb5411df9e5157ee0291a5'}
files = {}
syntax = []
hunks = 0
series.each do |name|
  owner = if name.start_with?('tfa-') then 'arm-trusted-firmware'
          elsif name.start_with?('edk2-') then 'edk2' else 'edk2-platforms' end
  lines = File.readlines(firmware+'/'+name)
  path = nil
  lines.each_with_index do |line,index|
    if line.start_with?('--- ')
      match=line.match(%r{\A--- a/([A-Za-z0-9_./+-]+)\n\z})
      abort "Unsupported old path in #{name}:#{index+1}" unless match
      path=match[1]
      abort "Unsafe path in #{name}" if path.start_with?('/') ||
        path.split('/').any? { |part| part.empty? || %w[. .. .git].include?(part) }
      abort "Renamed or unsupported new path in #{name}" unless lines[index+1]=="+++ b/#{path}\n"
    end
    next unless line.start_with?('@@ ')
    match=line.match(/^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/)
    abort "Invalid hunk header in #{name}:#{index+1}" unless match && path
    old_count=(match[2]||'1').to_i
    new_count=(match[4]||'1').to_i
    files[[owner,path]] ||= old_count
    body=lines[(index+1)..].take_while { |entry| !entry.start_with?('@@ ','--- a/','diff --git ') }
    body=body.take_while { |entry| entry.start_with?(' ','+','-','\\') }
    actual=[body.count { |entry| [' ','-'].include?(entry[0]) },
            body.count { |entry| [' ','+'].include?(entry[0]) }]
    syntax << "#{name}:#{index+1}: declares #{old_count}/#{new_count}, contains #{actual.join('/')}" unless actual==[old_count,new_count]
    hunks+=1
  end
  abort "No file hunks in #{name}" unless path
end
FileUtils.mkdir_p(output+'/series')
FileUtils.cp(firmware+'/series',output+'/series/series')
series.each { |name| FileUtils.cp(firmware+'/'+name,output+'/series/'+name) }
commands = []
run=lambda do |name,argv,input=nil,success=true|
  env={'GIT_NO_LAZY_FETCH'=>'1','GIT_OPTIONAL_LOCKS'=>'0','GIT_TERMINAL_PROMPT'=>'0'}
  out,err,status=Open3.capture3(env,*argv,stdin_data:input.to_s)
  commands << {name:name,argv:argv,environment:env,status:status.exitstatus,stdout:out,stderr:err}
  File.write(output+'/commands.json',JSON.pretty_generate(commands)+"\n")
  abort "#{name} failed (#{status.exitstatus}): #{out}#{err}" unless status.success? == success
  out
end
git=lambda do |owner,*args|
  ['git','--no-optional-locks','-c','protocol.allow=never','-C',File.join(source,owner),*args]
end
pins.each do |owner,revision|
  actual=run.call('pin-'+owner,git.call(owner,'rev-parse','HEAD')).strip
  abort "Wrong #{owner} pin: #{actual}" unless actual==revision
end
inputs = {}
files.each do |(owner,path),first_old_count|
  revision=pins.fetch(owner)
  entry=run.call('tree-'+owner+'/'+path,git.call(owner,'ls-tree',revision,'--',path))
  target=output+'/export/'+owner+'/'+path
  FileUtils.mkdir_p(File.dirname(target))
  if entry.empty?
    abort "Missing original file: #{owner}/#{path}" unless first_old_count==0
    inputs[owner+'/'+path]={new_file:true}
  else
    abort "Expected a regular source blob: #{owner}/#{path}" unless entry.match?(/\A100(?:644|755) blob [a-f0-9]{40}\t/)
    bytes=run.call('export-'+owner+'/'+path,git.call(owner,'show',revision+':'+path))
    File.binwrite(target,bytes)
    File.chmod(entry.start_with?('100755 ') ? 0o755 : 0o644,target)
    inputs[owner+'/'+path]={mode:entry.split.first,sha256:Digest::SHA256.hexdigest(bytes),bytes:bytes.bytesize}
  end
end
# Record count errors as well as the actual patch program's diagnostic.
File.write(output+'/hunk-count-errors.json',JSON.pretty_generate(syntax)+"\n")
series.each do |name|
  owner=if name.start_with?('tfa-') then 'arm-trusted-firmware'
        elsif name.start_with?('edk2-') then 'edk2' else 'edk2-platforms' end
  run.call('apply-'+name,['patch','-s','-t','-N','-p1','-F0','-d',output+'/export/'+owner],File.binread(firmware+'/'+name))
  puts 'PASS: '+name
end
abort 'Invalid hunk counts: '+syntax.join('; ') unless syntax.empty?
results=files.keys.to_h do |owner,path|
  target=output+'/export/'+owner+'/'+path
  abort "Missing output: #{owner}/#{path}" unless File.file?(target) && !File.symlink?(target)
  [owner+'/'+path,Digest::SHA256.file(target).hexdigest]
end
# Keep the current UEFI handoff policy across the complete ordered series.
config=File.read(output+'/export/edk2-platforms/Platform/RaspberryPi/RPi5/Drivers/RpiPlatformDxe/ConfigTable.c')
asi=File.read(output+'/export/edk2-platforms/Silicon/RaspberryPi/RpiSiliconPkg/Include/Rp1.asi')
strip_comments=lambda do |text|
  text.gsub(%r{//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"}m) do |token|
    token.start_with?('"') ? token : ' '*token.bytesize
  end
end
config=strip_comments.call(config)
asi=strip_comments.call(asi)
fan_contract={
  '125/255 PWM handoff'=>config.match?(/MmioWrite32\s*\(\s*Base\s*\+\s*RP1_PWM_CHANNEL3_DUTY,\s*\(RP1_FAN_PWM_RANGE\s*\*\s*125U\)\s*\/\s*255\s*\);/),
  'PWM channel enabled'=>config.match?(/Register\s*\|=\s*RP1_PWM_CHANNEL3_ENABLE\s*\|\s*RP1_PWM_SET_UPDATE;/),
  'obsolete LOW75 and off handoff absent'=>!config.include?('RP1_FAN_PWM_LOW') &&
    !config.match?(/RP1_PWM_CHANNEL3_DUTY,\s*0\s*\)/) &&
    !config.match?(/Register\s*&=\s*~RP1_PWM_CHANNEL3_ENABLE/),
  'original FRQS handoff mask'=>config.match?(/AcpiAmlObjectUpdateInteger\s*\(\s*AcpiSdtProtocol,\s*TableHandle,\s*"[^"]+\.FRQS",\s*0x1\s*\);/),
  'original FRQS cooling mapping'=>[[8,250],[4,175],[2,125],[1,75]].all? do |mask,duty|
    asi.match?(/If\s*\(FRQS\s*&\s*0x#{mask}\)\s*\{\s*Local0\s*=\s*\(FRNG\s*\*\s*#{duty}\)\s*\/\s*255/)
  end,
  'unavailable RPM correction'=>!asi.include?('FRPM') &&
    asi.match?(/Local1\s*=\s*0xFFFFFFFF\b/)
}
fan_contract.each do |name,valid|
  abort 'Fan source contract failed: '+name unless valid
  puts 'PASS: '+name
end
# A repeated patch must fail without a prompt or a reverse application.
repeat_file=output+'/repeat-control/make_helpers/utilities.mk'
FileUtils.mkdir_p(File.dirname(repeat_file))
FileUtils.cp(output+'/export/arm-trusted-firmware/make_helpers/utilities.mk',repeat_file)
repeat_sha=Digest::SHA256.file(repeat_file).hexdigest
run.call('reject-repeated-patch',['patch','-s','-t','-N','-p1','-F0','-d',output+'/repeat-control'],
  File.binread(firmware+'/tfa-shell-slice.diff'),false)
repeat_command=commands.last
abort 'Repeated patch prompted or reversed output' if
  (repeat_command[:stdout]+repeat_command[:stderr]).match?(/Assume -R|\[[yn]\]|\? /) ||
  Digest::SHA256.file(repeat_file).hexdigest!=repeat_sha
puts 'PASS: repeated patch fails without prompt or reverse'
File.write(output+'/receipt.json',JSON.pretty_generate({pins:pins,series:series,hunks:hunks,
  patch_sha256:series.to_h { |name| [name,Digest::SHA256.file(firmware+'/'+name).hexdigest] },
  inputs:inputs,outputs:results,fan_contract:fan_contract,repeat_control:true,scope:'All ordered patches applied forward with -t -N -p1 -F0 to only diff-referenced pinned Git blobs. No cache changes, fetch, reset, compiler, complete firmware build or device access.'})+"\n")
puts "PASS: #{series.length} ordered patches, #{hunks} hunks, #{files.length} source paths"
