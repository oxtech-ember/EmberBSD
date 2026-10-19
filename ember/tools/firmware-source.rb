#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted bounded firmware source preparation.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'optparse'
require 'rbconfig'

class FirmwareSource
  def self.lock_path(source)
    File.join(File.dirname(File.realpath(source)), '.ember-firmware.lock')
  end

  # The builder owns this same mkdir lease through its upstream EXIT trap.
  # Delegation is explicit and restricted to the recorded direct parent.
  def self.with_lock(source, parent = nil)
    path = lock_path(source)
    canonical = File.realpath(source)
    if parent
      raise 'Invalid parent lock owner' unless parent == Process.ppid &&
        File.directory?(path) && !File.symlink?(path) &&
        %w[owner source].all? { |name| File.file?(path+'/'+name) && !File.symlink?(path+'/'+name) } &&
        File.read(path+'/owner').strip == parent.to_s && File.read(path+'/source').strip == canonical
      return yield
    end
    begin
      Dir.mkdir(path, 0o700)
    rescue Errno::EEXIST
      raise 'Firmware cache is locked; do not steal a stale lease'
    end
    owned = true
    File.write(path+'/owner', Process.pid.to_s+"\n")
    File.write(path+'/source', canonical+"\n")
    yield
  ensure
    if owned
      valid=File.directory?(path) && !File.symlink?(path) &&
        %w[owner source].all? { |name| File.file?(path+'/'+name) && !File.symlink?(path+'/'+name) } &&
        File.read(path+'/owner').strip == Process.pid.to_s && File.read(path+'/source').strip == canonical
      raise 'Cache lease changed; preserve it for inspection' unless valid
      File.unlink(path+'/source')
      File.unlink(path+'/owner')
      Dir.rmdir(path)
    end
  end

  attr_reader :source, :state_path, :pending_path

  def initialize(source)
    @source = File.realpath(source)
    @state_path = File.join(File.dirname(@source), '.ember-firmware-source.json')
    @pending_path = File.join(File.dirname(@source), '.ember-firmware-pending.json')
  end

  def git(owner, *args)
    argv = ['git', '--no-optional-locks', '-c', 'protocol.allow=never',
      '-c', 'core.fileMode=true', '-C', File.join(source, owner), *args]
    out, err, status = Open3.capture3({'GIT_OPTIONAL_LOCKS'=>'0',
      'GIT_NO_LAZY_FETCH'=>'1', 'GIT_TERMINAL_PROMPT'=>'0'}, *argv)
    raise "Git failed: #{argv.inspect}: #{err}" unless status.success?
    out
  end

  def graph
    git('.', 'submodule', 'status', '--recursive').lines.to_h do |line|
      match = line.match(/\A([ +\-U])([a-f0-9]{40}) ([^\s]+)(?: |\n)/)
      raise 'Unsupported submodule state' unless match && [' ', '-'].include?(match[1])
      safe_path(match[3])
      if match[1]=='-' && (File.exist?(source+'/'+match[3]+'/.git') || File.symlink?(source+'/'+match[3]+'/.git'))
        raise 'Unregistered populated submodule must not be skipped'
      end
      [match[3], {'revision'=>match[2], 'present'=>match[1]==' '}]
    end
  end

  def safe_path(path)
    raise 'Unsafe source path' unless path.is_a?(String) &&
      path.match?(%r{\A[A-Za-z0-9_./+\-]+\z}) && !path.start_with?('/') &&
      path.split('/').none? { |part| part.empty? || %w[. .. .git].include?(part) }
    path
  end

  def target(path)
    safe_path(path)
    parts = path.split('/')
    cursor = source
    parts[0...-1].each do |part|
      cursor += '/'+part
      raise "Unsafe source parent: #{path}" if File.symlink?(cursor) ||
        (File.exist?(cursor) && !File.directory?(cursor))
    end
    source+'/'+path
  end

  def snapshot(path)
    file = target(path)
    return nil unless File.exist?(file) || File.symlink?(file)
    stat = File.lstat(file)
    raise "Expected a regular unlinked source file: #{path}" unless stat.file? && stat.nlink == 1
    raise "Unsupported special mode: #{path}" unless (stat.mode & 0o7000).zero?
    {'sha256'=>Digest::SHA256.file(file).hexdigest, 'mode'=>stat.mode & 0o777}
  end

  def read_json(path)
    raise "Unsafe metadata file: #{path}" unless File.file?(path) && !File.symlink?(path)
    JSON.parse(File.binread(path))
  end

  def expected(export)
    receipt = read_json(export+'/receipt.json')
    targets = receipt.fetch('outputs').to_h do |path, sha|
      safe_path(path)
      input = receipt.fetch('inputs').fetch(path)
      mode = input['mode'] == '100755' ? 0o755 : 0o644
      original = input['new_file'] ? nil : {'sha256'=>input.fetch('sha256'), 'mode'=>mode}
      file = export+'/export/'+path
      raise "Changed export: #{path}" unless File.file?(file) && !File.symlink?(file) &&
        Digest::SHA256.file(file).hexdigest == sha && (File.stat(file).mode & 0o777) == mode
      [path, {'sha256'=>sha, 'mode'=>mode, 'original'=>original}]
    end
    {'version'=>1, 'recipe_revision'=>receipt.fetch('recipe_revision'),
     'pins'=>receipt.fetch('pins'), 'series'=>receipt.fetch('series'),
     'patch_sha256'=>receipt.fetch('patch_sha256'),
     'targets'=>targets}
  end

  def audit(state, current_graph = graph)
    state.fetch('pins').each do |owner, revision|
      raise "Wrong source pin: #{owner}" unless git(owner, 'rev-parse', 'HEAD').strip == revision
    end
    raise 'Submodule graph changed' if state['graph'] && state['graph'] != current_graph
    owners = ['.']+current_graph.select { |_,entry| entry.fetch('present') }.keys
    owners.each do |owner|
      raise "Staged source changes: #{owner}" unless git(owner, 'diff', '--cached', '--name-only', '-z').empty?
      flags = git(owner, 'ls-files', '-v', '-z').split("\0")
      raise "Unsupported index flags: #{owner}" if flags.any? { |line| line.match?(/\A[a-zS]/) }
      git(owner, 'diff', '--no-ext-diff', '--no-textconv', '--name-only', '-z', 'HEAD').split("\0").each do |path|
        full = owner == '.' ? path : owner+'/'+path
        next if current_graph.key?(full)
        raise "Unknown tracked source change: #{full}" unless state.fetch('targets').key?(full)
      end
    end
    state.fetch('targets').each do |path, entry|
      wanted = entry.slice('sha256', 'mode')
      raise "Unexpected source state: #{path}" unless snapshot(path) == wanted
    end
    current_graph
  end

  # This compares saved build options. It is not a hermetic toolchain proof.
  def option_profile(receipt)
    compiler=receipt.fetch('compiler').slice('cross_compile','path','sha256','version','machine')
    raise 'Incomplete compiler identity' unless compiler.length == 5
    fs=receipt.fetch('freestanding')
    empty=fs.fetch('empty_root')
    tfa=fs.fetch('tfa_flags').split
    edk=fs.fetch('edk2_flags').split
    spec=tfa.select { |token| token.start_with?('LDFLAGS+=-specs=') }
    raise 'Unsupported specs role' unless spec.length == 1
    spec_path=spec.first.delete_prefix('LDFLAGS+=-specs=')
    roles={"CFLAGS+=--sysroot=#{empty}"=>'CFLAGS+=--sysroot=@EMPTY',
      "LDFLAGS+=--sysroot=#{empty}"=>'LDFLAGS+=--sysroot=@EMPTY',
      "LDFLAGS+=-specs=#{spec_path}"=>'LDFLAGS+=-specs=@SPECS'}
    roles.each_key { |token| raise 'Missing or duplicate typed TF-A role' unless tfa.count(token)==1 }
    edk_roles={"EMBER_FIRMWARE_ROOT=#{empty}"=>'EMBER_FIRMWARE_ROOT=@EMPTY',
      "EMBER_FIRMWARE_SPECS=#{spec_path}"=>'EMBER_FIRMWARE_SPECS=@SPECS'}
    edk_roles.each_key { |token| raise 'Missing or duplicate typed EDK2 role' unless edk.count(token)==1 }
    conf=%w[build_rule tools_def target].to_h do |name|
      bytes=git('edk2','show','HEAD:BaseTools/Conf/'+name+'.template')
      file=source+'/edk2/Conf/'+name+'.txt'
      if File.exist?(file) || File.symlink?(file)
        raise "Unknown Conf input: #{name}" unless File.file?(file) && !File.symlink?(file) && File.binread(file)==bytes
      end
      [name,Digest::SHA256.hexdigest(bytes)]
    end
    {'version'=>1,'variant'=>(receipt.fetch('variant').empty? ? 'standard' : receipt.fetch('variant')),'revision'=>receipt.fetch('revision'),
      'compiler'=>compiler,'include'=>fs.fetch('include'),
      'profile_sha256'=>fs.fetch('profile_sha256'),'specs_sha256'=>fs.fetch('specs_sha256'),
      'tfa_flags'=>tfa.map { |token| roles.fetch(token,token) },
      'edk2_flags'=>edk.map { |token| edk_roles.fetch(token,token) },'conf_templates'=>conf}
  end

  def legacy_profile(receipt, invocation, revision, expected)
    raise 'Unproved legacy build' unless invocation.fetch('source_revision')==revision &&
      invocation.fetch('source_clean')==true && receipt.fetch('revision')==expected.fetch('pins').fetch('.') &&
      receipt.fetch('series')==expected.fetch('patch_sha256') && invocation.fetch('series')==receipt.fetch('series')
    root=File.expand_path('../..',__dir__)
    blob=lambda do |path|
      out,err,status=Open3.capture3({'GIT_NO_LAZY_FETCH'=>'1','GIT_OPTIONAL_LOCKS'=>'0'},
        'git','--no-optional-locks','-C',root,'show',revision+':'+path)
      raise "Missing legacy source: #{err}" unless status.success?
      Digest::SHA256.hexdigest(out)
    end
    raise 'Legacy builder differs' unless blob.call('ember/build-firmware.sh')==invocation.fetch('builder_sha256')
    fs=receipt.fetch('freestanding')
    raise 'Legacy profile differs' unless blob.call('ember/firmware/gcc-profile.sh')==fs.fetch('profile_sha256') &&
      blob.call('ember/firmware/gcc.specs')==fs.fetch('specs_sha256')
    args=invocation.fetch('argv')
    option={''=>nil,'standard'=>nil,'rp1-console'=>'--rp1-console','rp1-trace'=>'--rp1-trace'}.fetch(receipt.fetch('variant'))
    raise 'Unsupported legacy invocation' unless args.length==(option ? 4 : 3) &&
      File.basename(args[0])=='bash' && args[1]=='ember/build-firmware.sh' &&
      args[2].start_with?('/') && File.realpath(args[2])==File.dirname(source) && (!option || args[3]==option) &&
      invocation.fetch('environment').fetch('CROSS_COMPILE')==receipt.fetch('compiler').fetch('cross_compile')
    # The saved receipt stores profile additions. The canonical argv fixes
    # the supported console and trace additions independently.
    full=Marshal.load(Marshal.dump(receipt))
    if option
      full['freestanding']['tfa_flags']='LOG_LEVEL=0 '+fs.fetch('tfa_flags')
      defines='-D RP1_UART_CONSOLE=TRUE'
      defines+=' -D CM5_BOOT_TRACE=TRUE' if option=='--rp1-trace'
      full['freestanding']['edk2_flags']=defines+' '+fs.fetch('edk2_flags')
    end
    option_profile(full)
  end

  def check_options(previous, raw)
    empty=raw.fetch('freestanding').fetch('empty_root')
    raise 'Current search root must be a real empty directory' unless
      File.directory?(empty) && !File.symlink?(empty) && Dir.empty?(empty)
    profile=option_profile(raw)
    if previous['option_profile']
      raise 'Build option profile changed; objects were preserved' unless previous['option_profile']==profile
    else
      raise 'Existing objects need an explicit saved build profile' if
        %w[Build arm-trusted-firmware/build edk2/BaseTools/Source/C/bin].any? { |path| File.exist?(source+'/'+path) }
      previous['option_profile']=profile
    end
  end

  def verify
    raise 'Pending source update requires explicit owner recovery' if File.exist?(pending_path) || File.symlink?(pending_path)
    state = read_json(state_path)
    raise 'Unsupported source manifest' unless state.fetch('version') == 1
    audit(state)
    state
  end

  def atomic_json(path, value)
    raise "Unsafe metadata target: #{path}" if File.symlink?(path)
    temporary = path+'.incoming'
    raise "Metadata staging collision: #{temporary}" if File.exist?(temporary) || File.symlink?(temporary)
    File.open(temporary, File::WRONLY|File::CREAT|File::EXCL, 0o600) do |file|
      file.write(JSON.pretty_generate(value)+"\n")
    end
    File.rename(temporary, path)
  end

  # Exported old/new recipes are immutable inputs from firmware-series.rb.
  # Each rename is atomic. A pending record blocks build after a partial batch.
  def update(previous, desired, export, work, apply: false)
    raise 'Pending source update requires explicit owner recovery' if File.exist?(pending_path) || File.symlink?(pending_path)
    current_graph = audit(previous)
    raise 'Recipe pins changed' unless previous.fetch('pins') == desired.fetch('pins')
    changes = []
    (previous.fetch('targets').keys | desired.fetch('targets').keys).each do |path|
      old = previous.fetch('targets')[path]
      new_entry = desired.fetch('targets')[path]
      if !new_entry && !old.fetch('original')
        raise "Removing a patch-created file is not supported: #{path}"
      end
      wanted = new_entry ? new_entry.slice('sha256', 'mode') : old.fetch('original')
      before = old ? old.slice('sha256', 'mode') : new_entry.fetch('original')
      raise "New target collision: #{path}" unless snapshot(path) == before
      next if before == wanted
      if new_entry
        bytes = File.binread(export+'/export/'+path)
      else
        owner = desired.fetch('pins').keys.reject { |p| p=='.' }.find { |p| path.start_with?(p+'/') }
        raise 'Unknown patch owner' unless owner
        bytes = git(owner, 'show', desired.fetch('pins').fetch(owner)+':'+path.delete_prefix(owner+'/'))
      end
      raise "Desired source hash changed: #{path}" unless Digest::SHA256.hexdigest(bytes) == wanted.fetch('sha256')
      changes << {'path'=>path, 'before'=>before, 'after'=>wanted, 'bytes'=>bytes}
    end
    desired = desired.merge('graph'=>current_graph)
    desired['option_profile']=previous['option_profile'] if previous['option_profile']
    plan = {'version'=>1, 'source'=>source, 'changes'=>changes.map { |c| c.reject { |k,_| k=='bytes' } }, 'state'=>desired}
    File.write(work+'/plan.json', JSON.pretty_generate(plan)+"\n")
    return plan unless apply
    # Keep old bytes before the first shared-source mutation.
    changes.each_with_index do |change, index|
      file = target(change.fetch('path'))
      File.binwrite(work+"/before-#{index}", File.binread(file)) if change['before']
      File.binwrite(work+"/after-#{index}", change.fetch('bytes'))
    end
    atomic_json(pending_path, plan.merge('work'=>File.realpath(work)))
    changes.each_with_index do |change, index|
      path = change.fetch('path')
      raise "Source changed before publish: #{path}" unless snapshot(path) == change['before']
      file = target(path)
      FileUtils.mkdir_p(File.dirname(file))
      target(path)
      temporary = file+'.ember-incoming'
      raise "Source staging collision: #{path}" if File.exist?(temporary) || File.symlink?(temporary)
      File.open(temporary, File::WRONLY|File::CREAT|File::EXCL, change.fetch('after').fetch('mode')) do |output|
        output.write(change.fetch('bytes'))
      end
      File.chmod(change.fetch('after').fetch('mode'), temporary)
      File.rename(temporary, file)
    end
    audit(desired, current_graph)
    atomic_json(state_path, desired)
    File.unlink(pending_path)
    plan
  end
end

if $PROGRAM_NAME == __FILE__
  options = {apply:false}
  parser = OptionParser.new do |p|
    p.banner = 'usage: firmware-source.rb SOURCE EMPTY_OUTPUT [--previous-ref COMMIT] [--apply]'
    p.on('--previous-ref COMMIT') { |value| options[:previous]=value }
    p.on('--previous-build-receipt FILE') { |value| options[:receipt]=value }
    p.on('--previous-build-invocation FILE') { |value| options[:invocation]=value }
    p.on('--build-profile FILE') { |value| options[:profile]=value }
    p.on('--apply') { options[:apply]=true }
    p.on('--verify-state') { options[:verify]=true }
    p.on('--parent-lock PID', Integer) { |value| options[:parent]=value }
  end
  begin
    parser.parse!
    raise parser.to_s unless ARGV.length == 2
    source,work=ARGV
    source=File.realpath(source)
    work=File.expand_path(work)
    raise 'Output must not be a symlink' if File.symlink?(work)
    work=File.join(File.realpath(File.dirname(work)),File.basename(work))
    raise 'Source and output must not overlap' if work==source ||
      work.start_with?(source+'/') || source.start_with?(work+'/')
    unless options[:verify]
      raise 'Output must be empty' if File.exist?(work) && (!File.directory?(work) || !Dir.empty?(work))
    end
    FirmwareSource.with_lock(source, options[:parent]) do
      tool=FirmwareSource.new(source)
      if options[:verify]
        raise 'Conflicting source options' if options[:previous] || options[:apply]
        tool.verify
        puts 'PASS: exact prepared source state'
        next
      end
      FileUtils.mkdir_p(work)
      exporter=File.join(__dir__,'firmware-series.rb')
      export=lambda do |directory, revision=nil|
        argv=[RbConfig.ruby,exporter,tool.source,directory]
        argv+=['--recipe-ref',revision] if revision
        out,err,status=Open3.capture3(*argv)
        File.write(directory+'.log',out+err)
        raise "Series export failed: #{out}#{err}" unless status.success?
        tool.expected(directory)
      end
      desired=export.call(work+'/desired')
      committed=export.call(work+'/committed', desired.fetch('recipe_revision'))
      raise 'Current series must match its committed recipe' unless committed == desired
      if File.exist?(tool.state_path) || File.symlink?(tool.state_path)
        raise 'Previous ref conflicts with source manifest' if options[:previous]
        previous=tool.read_json(tool.state_path)
        proved=export.call(work+'/previous', previous.fetch('recipe_revision'))
        raise 'Source manifest does not match its immutable recipe' unless
          previous.reject { |key,_| %w[graph option_profile].include?(key) } == proved
      elsif options[:previous]
        previous=export.call(work+'/previous',options[:previous])
      else
        # First preparation accepts only pristine pinned source bytes.
        previous=desired.merge('targets'=>desired.fetch('targets').map do |path,entry|
          entry['original'] ? [path,entry['original'].merge('original'=>entry['original'])] : nil
        end.compact.to_h)
      end
if options[:apply] && options[:previous] && !options[:receipt] &&
    %w[Build arm-trusted-firmware/build edk2/BaseTools/Source/C/bin].any? { |path| File.exist?(tool.source+'/'+path) }
  raise 'Legacy objects need receipt and invocation at initial adoption'
end
      if options[:receipt] || options[:invocation]
        raise 'Legacy profile needs previous-ref, receipt and invocation' unless options[:previous] && options[:receipt] && options[:invocation]
        previous['option_profile']=tool.legacy_profile(tool.read_json(options[:receipt]),
          tool.read_json(options[:invocation]),options[:previous],previous)
      end
      tool.check_options(previous,tool.read_json(options[:profile])) if options[:profile]
      plan=tool.update(previous,desired,work+'/desired',work,apply:options[:apply])
      puts "PASS: #{plan.fetch('changes').length} verified source replacements#{options[:apply] ? ' applied' : ' planned'}"
    end
  rescue StandardError => error
    warn "firmware source: #{error.message}"
    exit 1
  end
end
