#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted actual-source state and lease controls.
helper=ENV.fetch('FIRMWARE_SOURCE_HELPER',File.expand_path('firmware-source.rb',__dir__))
require helper
require 'tmpdir'

mutants=ARGV.delete('--mutants')
abort 'usage: firmware-source-contract.rb EMPTY_OUTPUT [--mutants]' unless ARGV.length == 1
output=File.expand_path(ARGV[0])
abort 'Output must be a real empty directory' if File.symlink?(output) ||
  (File.exist?(output) && (!File.directory?(output) || !Dir.empty?(output)))
FileUtils.mkdir_p(output)
checks=0
records=[]
run=lambda do |directory,*args|
  env={'GIT_AUTHOR_NAME'=>'EmberBSD fixture','GIT_AUTHOR_EMAIL'=>'fixture@example.invalid',
       'GIT_COMMITTER_NAME'=>'EmberBSD fixture','GIT_COMMITTER_EMAIL'=>'fixture@example.invalid'}
  out,err,status=Open3.capture3(env,'git','-c','commit.gpgsign=false',
    '-c','core.hooksPath=/dev/null','-C',directory,*args)
  raise "Fixture Git failed: #{args.inspect}: #{out}#{err}" unless status.success?
  out
end
base=output+'/base/cache/source'
FileUtils.mkdir_p(base+'/edk2/BaseTools/Conf')
[base+'/edk2',base].each { |path| run.call(path,'init','-q','-b','main') }
File.write(base+'/edk2/changed','original')
File.write(base+'/edk2/other','unchanged')
File.write(base+'/edk2/wrapper','fixture executable source')
File.chmod(0o755,base+'/edk2/wrapper')
%w[build_rule tools_def target].each { |name| File.write(base+'/edk2/BaseTools/Conf/'+name+'.template',name+" template\n") }
run.call(base+'/edk2','add','.')
run.call(base+'/edk2','commit','-qm','fixture original')
File.write(base+'/.gitmodules',"[submodule \"edk2\"]\n path = edk2\n url = https://example.invalid/fixture\n")
File.write(base+'/.gitignore',"/ignored-output\n")
File.write(base+'/build.sh','original top')
run.call(base,'add','.')
run.call(base,'commit','-qm','fixture top')
run.call(base,'config','submodule.edk2.url','https://example.invalid/fixture')
File.write(base+'/edk2/changed','accepted old')
File.write(base+'/user-file','unique untracked')
File.write(base+'/ignored-output','unique ignored')
FileUtils.mkdir_p(File.dirname(base)+'/objects')
File.write(File.dirname(base)+'/objects/keep.o','accepted object')
pins={'.'=>run.call(base,'rev-parse','HEAD').strip,'edk2'=>run.call(base+'/edk2','rev-parse','HEAD').strip}
sha=lambda { |bytes| Digest::SHA256.hexdigest(bytes) }
original={'sha256'=>sha.call('original'),'mode'=>0o644}
wrapper={'sha256'=>sha.call('fixture executable source'),'mode'=>0o755}
old={'version'=>1,'recipe_revision'=>'a'*40,'pins'=>pins,'patch_sha256'=>{'fixture.diff'=>'a'*64},
 'targets'=>{'edk2/wrapper'=>wrapper.merge('original'=>wrapper),'edk2/changed'=>{'sha256'=>sha.call('accepted old'),'mode'=>0o644,'original'=>original}}}
newer=Marshal.load(Marshal.dump(old))
newer['recipe_revision']='b'*40
newer['patch_sha256']={'fixture.diff'=>'b'*64}
newer['targets']['edk2/changed']['sha256']=sha.call('new source')
newer['targets']['edk2/new/created']={'sha256'=>sha.call('new file'),'mode'=>0o644,'original'=>nil}

# Each case uses the actual helper and real Git indexes/worktrees. No network,
# firmware compiler, upstream build, reset, clean or common cache is used.
test=lambda do |name,&body|
  directory=output+'/'+name
  FileUtils.mkdir_p(directory)
  FileUtils.cp_r(output+'/base/cache',directory+'/cache')
  source=directory+'/cache/source'
  work=directory+'/work'
  FileUtils.mkdir_p(work+'/export/export/edk2/new')
  File.write(work+'/export/export/edk2/changed','new source')
  File.write(work+'/export/export/edk2/new/created','new file')
  tool=FirmwareSource.new(source)
  previous=Marshal.load(Marshal.dump(old))
  desired=Marshal.load(Marshal.dump(newer))
  assert=lambda { |value,message| raise message unless value; checks+=1 }
  reject=lambda do |&operation|
    begin
      operation.call
    rescue StandardError => error
      checks+=1
      next error.message
    end
    raise 'Expected a controlled refusal'
  end
  sentinels=%w[user-file ignored-output].map { |path| source+'/'+path }+[directory+'/cache/objects/keep.o']
  before=sentinels.to_h { |path| [path,[File.binread(path),File.stat(path).mtime]] }
  body.call(tool,previous,desired,work,assert,reject,source)
  assert.call(before.all? { |path,state| [File.binread(path),File.stat(path).mtime]==state },'unrelated object or unique file changed')
  records<<{name:name,status:'PASS'}
  puts 'PASS: '+name
end

apply=lambda { |tool,old_state,new_state,work| tool.update(old_state,new_state,work+'/export',work,apply:true) }
test.call('top-level-patch-restoration') do |tool,previous,_,work,assert,_,source|
  desired=Marshal.load(Marshal.dump(previous))
  original={'sha256'=>sha.call('original top'),'mode'=>0o644}
  desired['targets']['build.sh']={'sha256'=>sha.call('patched top'),'mode'=>0o644,'original'=>original}
  File.write(work+'/export/export/build.sh','patched top')
  FirmwareSource.with_lock(source) { tool.update(previous,desired,work+'/export',work,apply:true) }
  assert.call(File.read(source+'/build.sh')=='patched top','top source not patched')
  restored=tool.verify
  FirmwareSource.with_lock(source) { tool.update(restored,previous,work+'/export',work,apply:true) }
  assert.call(File.read(source+'/build.sh')=='original top','top source not restored')
end
test.call('apply-idempotent') do |tool,previous,desired,work,assert,_,source|
  FirmwareSource.with_lock(source) { apply.call(tool,previous,desired,work) }
  assert.call(File.read(source+'/edk2/changed')=='new source','wrong published source')
  assert.call(File.read(source+'/edk2/new/created')=='new file','new parent/file not published')
  state=tool.verify
  mtime=File.stat(source+'/edk2/changed').mtime
  FirmwareSource.with_lock(source) { apply.call(tool,state,desired,work) }
  assert.call(File.stat(source+'/edk2/changed').mtime==mtime,'idempotent source touched')
  assert.call(!File.exist?(FirmwareSource.lock_path(source)),'lease leaked')
end
test.call('dry-run') do |tool,previous,desired,work,assert,_,source|
  FirmwareSource.with_lock(source) { tool.update(previous,desired,work+'/export',work) }
  assert.call(File.read(source+'/edk2/changed')=='accepted old' && !File.exist?(tool.state_path),'dry run wrote source state')
end
{
 'unknown-tracked'=>lambda { |source| File.write(source+'/edk2/other','unique tracked edit') },
 'staged'=>lambda { |source| run.call(source+'/edk2','add','changed') },
 'special-mode'=>lambda { |source| File.chmod(0o4755,source+'/edk2/wrapper') },
 'mode'=>lambda { |source| File.chmod(0o755,source+'/edk2/changed') },
 'target-symlink'=>lambda { |source| File.unlink(source+'/edk2/changed'); File.symlink('other',source+'/edk2/changed') },
 'hardlink'=>lambda { |source| File.link(source+'/edk2/changed',source+'/edk2/copy') },
 'new-collision'=>lambda { |source| FileUtils.mkdir_p(source+'/edk2/new'); File.write(source+'/edk2/new/created','unique new target') },
 'parent-symlink'=>lambda { |source| File.symlink(File.dirname(source)+'/objects',source+'/edk2/new') },
 'parent-file'=>lambda { |source| File.write(source+'/edk2/new','unique parent') },
 'pending'=>lambda { |source| File.write(File.dirname(source)+'/.ember-firmware-pending.json','preserved pending') },
 'unregistered-populated'=>lambda { |source| run.call(source,'config','--unset','submodule.edk2.url') },
 'index-assume-unchanged'=>lambda { |source| run.call(source+'/edk2','update-index','--assume-unchanged','other'); File.write(source+'/edk2/other','hidden edit') }
}.each do |name,alter|
  test.call(name) do |tool,previous,desired,work,assert,reject,source|
    alter.call(source)
    reject.call { apply.call(tool,previous,desired,work) }
    assert.call(!File.exist?(tool.state_path),'refusal published a state')
    assert.call(!File.exist?(tool.pending_path),'preflight refusal wrote pending state') unless name=='pending'
    assert.call(!File.exist?(source+'/edk2/new/created'),'refusal wrote a new target') unless name=='new-collision'
  end
end
test.call('wrong-pin') do |tool,previous,desired,work,assert,reject,_|
  previous['pins']['edk2']='c'*40
  reject.call { apply.call(tool,previous,desired,work) }
  assert.call(!File.exist?(tool.pending_path),'wrong pin reached publication')
end
test.call('created-file-removal') do |tool,previous,desired,work,assert,reject,source|
  previous['targets']['edk2/created']={'sha256'=>sha.call('created old'),'mode'=>0o644,'original'=>nil}
  File.write(source+'/edk2/created','created old')
  reject.call { apply.call(tool,previous,desired,work) }
  assert.call(File.read(source+'/edk2/created')=='created old','removed unique source')
end
test.call('removed-tracked-patch') do |tool,previous,desired,work,assert,_,source|
  desired['targets'].delete('edk2/changed')
  apply.call(tool,previous,desired,work)
  assert.call(File.read(source+'/edk2/changed')=='original','did not restore exact pinned original')
end
test.call('interrupted-publication') do |tool,previous,desired,work,assert,reject,source|
  class << tool
    def atomic_json(path,value)
      raise IOError,'fixture state publication failure' if path==state_path
      super
    end
  end
  reject.call { apply.call(tool,previous,desired,work) }
  assert.call(File.exist?(tool.pending_path) && !File.exist?(tool.state_path),'partial batch looks committed')
  assert.call(File.read(work+'/before-0')=='accepted old','lost pre-update source')
  reject.call { tool.verify }
  reject.call { apply.call(tool,previous,desired,work) }
  assert.call(File.read(source+'/edk2/changed')=='new source','pending check overwrote mixed state')
end
test.call('interrupted-source-rename') do |tool,previous,desired,work,assert,reject,source|
  rename=File.method(:rename)
  File.define_singleton_method(:rename) do |from,to|
    raise IOError,'fixture second-source rename failure' if to==source+'/edk2/new/created'
    rename.call(from,to)
  end
  begin
    reject.call { apply.call(tool,previous,desired,work) }
  ensure
    File.define_singleton_method(:rename,rename)
  end
  assert.call(File.read(source+'/edk2/changed')=='new source' && !File.exist?(source+'/edk2/new/created'),'not a partial source batch')
  assert.call(File.exist?(tool.pending_path) && !File.exist?(tool.state_path),'partial source rename looked committed')
  assert.call(File.read(work+'/before-0')=='accepted old','lost original source after partial rename')
  reject.call { tool.verify }
end

test.call('lease-conflict') do |_,_,_,_,assert,reject,source|
  path=FirmwareSource.lock_path(source)
  Dir.mkdir(path)
  File.write(path+'/owner','stale owner')
  reject.call { FirmwareSource.with_lock(source) { raise 'entered busy lease' } }
  assert.call(File.read(path+'/owner')=='stale owner','stole stale lease')
end
test.call('explicit-parent-lease') do |_,_,_,work,assert,_,source|
  FirmwareSource.with_lock(source) do
    code='require ARGV.shift; FirmwareSource.with_lock(ARGV.shift,Integer(ARGV.shift)) { puts "delegated" }'
    out,err,status=Open3.capture3(RbConfig.ruby,'-e',code,helper,source,Process.pid.to_s)
    assert.call(status.success? && out=="delegated\n",'valid direct-parent delegation failed: '+err)
    _,_,status=Open3.capture3(RbConfig.ruby,'-e',code,helper,source,(Process.pid+1).to_s)
    assert.call(!status.success?,'wrong owner bypassed lease')
    middle='require "rbconfig"; ok=system(RbConfig.ruby,*ARGV); exit(ok ? 0 : 1)'
    _,err,status=Open3.capture3(RbConfig.ruby,'-e',middle,'--','-e',code,helper,source,Process.pid.to_s)
    assert.call(!status.success? && err.include?('Invalid parent lock owner'),'grandchild inherited delegation without direct-parent proof')

    code='require ARGV.shift; FirmwareSource.with_lock(ARGV.shift) { puts "bypass" }'
    _,_,status=Open3.capture3({'FIRMWARE_PARENT_LOCK'=>Process.pid.to_s},RbConfig.ruby,'-e',code,helper,source)
    assert.call(!status.success?,'environment bypassed lease')
  end
end

test.call('changed-lease-owner') do |_,_,_,_,assert,reject,source|
  path=FirmwareSource.lock_path(source)
  reject.call do
    FirmwareSource.with_lock(source) { File.write(path+'/owner','new owner') }
  end
  assert.call(File.read(path+'/owner')=='new owner' && File.exist?(path+'/source'),'changed lease was erased')
end
test.call('yield-eexist') do |_,_,_,_,assert,reject,source|
  message=reject.call { FirmwareSource.with_lock(source) { raise Errno::EEXIST,'inner fixture' } }
  assert.call(message.include?('inner fixture') && !message.include?('cache is locked'),'misclassified yield failure')
  assert.call(!File.exist?(FirmwareSource.lock_path(source)),'own lease leaked on yield failure')
end
test.call('output-parent-symlink') do |_,_,_,work,assert,_,source|
  File.symlink(source,work+'/link')
  _,err,status=Open3.capture3(RbConfig.ruby,helper,source,work+'/link/unsafe')
  assert.call(!status.success? && err.include?('Source and output must not overlap'),'CLI did not reject aliased output')
  assert.call(!File.exist?(source+'/unsafe') && !File.exist?(FirmwareSource.lock_path(source)),'invalid output created source or lease')
end

test.call('ordered-recipe') do |tool,previous,_,work,assert,_,_|
  receipt={'recipe_revision'=>'a'*40,'pins'=>previous.fetch('pins'),
    'series'=>%w[first.diff second.diff], 'patch_sha256'=>{'first.diff'=>'a'*64,'second.diff'=>'b'*64},
    'inputs'=>{'edk2/changed'=>{'mode'=>'100644','sha256'=>sha.call('original')}},
    'outputs'=>{'edk2/changed'=>sha.call('new source')}}
  File.write(work+'/export/receipt.json',JSON.generate(receipt))
  first=tool.expected(work+'/export')
  receipt['series'].reverse!
  File.write(work+'/export/receipt.json',JSON.generate(receipt))
  second=tool.expected(work+'/export')
  assert.call(first.fetch('outputs',first['targets'])==second.fetch('outputs',second['targets']),'control outputs changed')
  assert.call(first!=second,'commuting reorder lost committed recipe order')
end

test.call('clang-option-profile') do |tool,previous,_,work,assert,reject,source|
  empty=work+'/empty'; Dir.mkdir(empty)
  raw={'variant'=>'','revision'=>'a'*40,
    'compiler'=>{'family'=>'clang','path'=>'/prefix/bin/clang','version'=>'23.1.4',
      'machine'=>'aarch64-unknown-none-elf','sha256'=>'b'*64},
    'freestanding'=>{'include'=>'/prefix/lib/clang/23/include','empty_root'=>empty,
      'profile_sha256'=>'c'*64,
      'tfa_flags'=>"CFLAGS+=--sysroot=#{empty} LDFLAGS+=--sysroot=#{empty} CC=/prefix/bin/clang",
      'edk2_flags'=>"-D EMBER_CLANG_FIRMWARE_ROOT=#{empty}"}}
  template=work+'/export/export/edk2/BaseTools/Conf/tools_def.template'
  FileUtils.mkdir_p(File.dirname(template))
  File.write(template, 'patched template')
  FileUtils.mkdir_p(source+'/Conf')
  File.write(source+'/Conf/tools_def.txt', 'patched template')
  previous['option_profile']=tool.option_profile(raw,work+'/export/export')
  same=Marshal.load(Marshal.dump(raw)); other=work+'/other'; Dir.mkdir(other)
  same['freestanding']['empty_root']=other
  %w[tfa_flags edk2_flags].each { |k| same['freestanding'][k]=same['freestanding'][k].gsub(empty,other) }
  tool.check_options(previous,same,work+'/export/export')
  assert.call(true,'equivalent Clang search root refused')
  bad=Marshal.load(Marshal.dump(same)); bad['compiler']['machine']='aarch64-unknown-linux-gnu'
  reject.call { tool.check_options(previous,bad,work+'/export/export') }
  File.write(source+'/Conf/tools_def.txt', 'unknown config')
  reject.call { tool.check_options(previous,same,work+'/export/export') }
end

test.call('option-profile') do |tool,previous,_,work,assert,reject,source|
  empty=work+'/empty';Dir.mkdir(empty)
  raw={'variant'=>'','revision'=>'pinned','compiler'=>{'path'=>'/compiler','version'=>'16.2.0','machine'=>'aarch64--netbsd','sha256'=>'a'*64,'cross_compile'=>'/prefix-'},
    'freestanding'=>{'include'=>'/same/include','empty_root'=>empty,'specs_sha256'=>'b'*64,'profile_sha256'=>'c'*64,
      'tfa_flags'=>"CFLAGS+=--sysroot=#{empty} LDFLAGS+=--sysroot=#{empty} LDFLAGS+=-specs=/specs",
      'edk2_flags'=>"-D EMBER_FIRMWARE_ROOT=#{empty} -D EMBER_FIRMWARE_SPECS=/specs"}}
  previous['option_profile']=tool.option_profile(raw)
  tool.check_options(previous,raw)
  same=Marshal.load(Marshal.dump(raw));other=work+'/other-empty';Dir.mkdir(other)
  same['freestanding']['empty_root']=other
  same['freestanding']['tfa_flags']=same['freestanding']['tfa_flags'].gsub(empty,other).sub('/specs','/other-specs')
  same['freestanding']['edk2_flags']=same['freestanding']['edk2_flags'].gsub(empty,other).sub('/specs','/other-specs')
  tool.check_options(previous,same)
  assert.call(true,'typed role change refused')
  %w[variant prefix sha version include tfa edk specs profile].each do |field|
    bad=Marshal.load(Marshal.dump(raw))
    case field
    when 'variant' then bad['variant']='rp1-console'
    when 'prefix' then bad['compiler']['cross_compile']='/other-'
    when 'sha','version' then bad['compiler'][field=='sha' ? 'sha256' : 'version']='other'
    when 'include' then bad['freestanding']['include']='/other/include'
    when 'tfa','edk' then bad['freestanding'][(field=='tfa' ? 'tfa_flags' : 'edk2_flags')]+=' unsupported=1'
    when 'specs','profile' then bad['freestanding'][field+'_sha256']='other'
    end
    reject.call { tool.check_options(previous,bad) }
  end
  root=File.expand_path('../..',__dir__)
  revision=run.call(root,'rev-parse','HEAD').strip
  legacy=Marshal.load(Marshal.dump(raw))
  legacy['revision']=previous.fetch('pins').fetch('.')
  legacy['series']=previous.fetch('patch_sha256')
  %w[profile specs].each do |role|
    path=role=='profile' ? 'ember/firmware/gcc-profile.sh' : 'ember/firmware/gcc.specs'
    legacy['freestanding'][role+'_sha256']=Digest::SHA256.hexdigest(run.call(root,'show',revision+':'+path))
  end
  invocation={'source_revision'=>revision,'source_clean'=>true,
   'argv'=>['bash','ember/build-firmware.sh',File.dirname(source)],
   'builder_sha256'=>Digest::SHA256.hexdigest(run.call(root,'show',revision+':ember/build-firmware.sh')),
   'series'=>legacy['series'],'environment'=>{'CROSS_COMPILE'=>legacy['compiler']['cross_compile']}}
  tool.legacy_profile(legacy,invocation,revision,previous)
  wrong=Marshal.load(Marshal.dump(invocation));wrong['argv'][2]=work
  reject.call { tool.legacy_profile(legacy,wrong,revision,previous) }
  wrong=Marshal.load(Marshal.dump(invocation));wrong['builder_sha256']='unknown'
  reject.call { tool.legacy_profile(legacy,wrong,revision,previous) }
  FileUtils.mkdir_p(source+'/Build')
  reject.call { tool.check_options(previous.reject { |key,_| key=='option_profile' },raw) }
  FileUtils.mkdir_p(source+'/Conf')
  File.write(source+'/Conf/target.txt','unique user configuration')
  reject.call { tool.check_options(previous,raw) }
end
File.write(output+'/receipt.json',JSON.pretty_generate({checks:checks,cases:records,
  helper_sha256:Digest::SHA256.file(helper).hexdigest,
  scope:'Actual Ruby helper, real bounded Git fixtures, no network, common objects, firmware build or devices.'})+"\n")
puts "PASS: #{checks} assertions across #{records.length} source controls"

if mutants
  root=File.expand_path('../..',__dir__)
  git_dir=run.call(root,'rev-parse','--absolute-git-dir').strip
  original=File.binread(helper)
  mutations={
    'unknown-tracked'=>['raise "Unknown tracked source change: #{full}" unless state.fetch(\'targets\').key?(full)','nil'],
    'new-collision'=>['raise "New target collision: #{path}" unless snapshot(path) == before','nil'],
    'mode'=>["'mode'=>stat.mode & 0o777","'mode'=>0o644"],
    'special-mode'=>['raise "Unsupported special mode: #{path}" unless (stat.mode & 0o7000).zero?','nil'],
    'order'=>["'series'=>receipt.fetch('series'),",''],

    'unregistered'=>["raise 'Unregistered populated submodule must not be skipped'",'nil'],
    'parent-proof'=>['parent == Process.ppid &&','true &&'],
    'owner-cleanup'=>["File.read(path+'/owner').strip == Process.pid.to_s &&",'true &&'],
    'cache-binding'=>['File.realpath(args[2])==File.dirname(source) &&','true &&'],
    'option-guard'=>["raise 'Build option profile changed; objects were preserved' unless previous['option_profile']==profile",'nil']
  }
  # A copied positive closes the provenance/path boundary for these mutants.
  variants={'copied-positive'=>nil}.merge(mutations)
  variants.each do |name,change|
    base=output+'/mutants/'+name
    FileUtils.mkdir_p(base+'/ember/tools')
    File.symlink(git_dir,base+'/.git')
    file=base+'/ember/tools/firmware-source.rb'
    content=original
    if change
      count=content.scan(Regexp.new(Regexp.escape(change[0]))).length
      raise "Mutant anchor must be unique: #{name}: #{count}" unless count==1
      content=content.sub(change[0]) { change[1] }
    end
    File.binwrite(file,content)
    out,err,status=Open3.capture3(RbConfig.ruby,'-c',file)
    raise 'Mutant syntax failed: '+name+out+err unless status.success?
    out,err,status=Open3.capture3({'FIRMWARE_SOURCE_HELPER'=>file},RbConfig.ruby,
      File.expand_path(__FILE__),base+'/controls')
    File.write(base+'/run.log',out+err)
    raise "Unexpected mutant behavior: #{name}: #{status.exitstatus}" unless status.success? == change.nil?
    puts "PASS: #{name}: #{change ? 'compiled and behaviorally rejected' : 'executed'}"
  end
end
