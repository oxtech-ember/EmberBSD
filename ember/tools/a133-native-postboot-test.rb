#!/usr/bin/env ruby
# Origin: EmberBSD - crash-aware native acceptance through the cable owner.
require 'tmpdir'
require 'zlib'
require_relative 'a133-cable-install'
path=File.join(__dir__,'a133-native-postboot.rb')
abort 'FAIL: native postboot owner is missing' unless File.file?(path)
require path
class NativeFixture
  attr_reader :writes,:confirmations,:observations,:host_key_sha256,:kernel_version_sha256
  attr_accessor :marker,:epoch,:fail_write,:accept,:fail_confirm,:fail_confirm_after
  def initialize(env,nonce)
    @env=env.dup;@marker=nonce;@epoch='fixture-boot-epoch';@writes=0;@confirmations=0;@observations=0
    @host_key_sha256='2'*64;@kernel_version_sha256='9'*64;@accept=true
  end
  def observe(policy:,boot_sha256:,nonce:)
    @observations+=1
    raise 'fixture binding lost' unless policy.is_a?(A133Recovery::Policy) && boot_sha256=='3'*64 && nonce=='4'*48
    {epoch:@epoch,marker:@marker}
  end
  def environment;@env.dup;end
  def confirm(nonce:,epoch:)
    raise 'fixture stale marker' unless @epoch==epoch && @marker==nonce
    raise A133Cable::Invalid,'native_connection_lost' if @fail_confirm
    @confirmations+=1;@marker=nil
    raise A133Cable::Invalid,'native_connection_lost' if @fail_confirm_after
    true
  end
  def write_environment(prefix:,epoch:)
    raise 'fixture stale epoch' unless @epoch==epoch
    @writes+=1
    @env=prefix+@env.byteslice(131072..)
    raise A133Cable::Invalid,'native_connection_lost' if @fail_write
  end
end
checks=0
fail_case=lambda do |reason,&block|
  begin
    block.call;abort 'FAIL: native postboot accepted invalid state'
  rescue A133Cable::Invalid=>error
    abort "FAIL: expected #{reason}, received #{error.message}" unless error.message==reason && error.cause.nil?
  end
  checks+=1
end
factory=A133Recovery::DIRECT_PROFILE.merge('setargs_nand'=>A133Recovery::DIRECT_SETARGS,
  'opaque'=>"\xffkeep".b,'empty'=>'')
body=factory.map { |k,v| k.b+'='+v.b+"\0" }.join+"\0"
body=body.ljust(131068,"\xff".b)
original=[Zlib.crc32(body)].pack('V')+body+"\xa5".b*(16777216-131072)
sha=lambda { |v| Digest::SHA256.hexdigest(v) }
backup={'serial'=>'POSTBOOT_TEST','cid'=>'1'*32,'status'=>'backup_integrity_verified',
  'bytes'=>A133Backup::BYTES,'uncompressed_sha256'=>'a'*64,'gpt_sha256'=>'b'*64,
  'gpt_headers_crc'=>true,'gpt_arrays_crc'=>true,'partition_layout_verified'=>true,
  'critical_copies_verified'=>true,'hardware_boot_copies_verified'=>true,
  'partition_sha256'=>{'boot'=>'c'*64,'bootloader'=>'d'*64,'recovery'=>'e'*64,'env'=>sha.call(original)}}
policy=A133Recovery::Policy.new(serial:'POSTBOOT_TEST',cid:'1'*32,original_env:original,backup:backup)
rearmed=A133Recovery.trial(original.byteslice(0,131072))[:rearmed_env]+original.byteslice(131072..)
image_context={'schema'=>1,'board'=>'ys-m33-a133','serial'=>'POSTBOOT_TEST','cid'=>'1'*32,
  'source_commit'=>'f'*40,'backup_sha256'=>'a'*64,'recovery_sha256'=>'e'*64,
  'protected_env_sha256'=>sha.call(A133Recovery.protect(original.byteslice(0,131072))),
  'artifacts'=>%w[root boot resources].map { |role| {'role'=>role,'bytes'=>role=='root' ? 1024 : 33554432,'sha256'=>role=='boot' ? '3'*64 : '5'*64} }}
receipt={'writes_performed'=>0,'installation_ready'=>false,'status'=>'artifact_manifest_verified',
  'source_commit'=>'f'*40,'artifacts'=>image_context['artifacts'].map do |row|
    row.merge('start_sector'=>{'root'=>6565888,'boot'=>172032,'resources'=>73728}.fetch(row['role']))
  end}
cable={'serial'=>'POSTBOOT_TEST','cid'=>'1'*32,'backup_sha256'=>'a'*64,'gpt_sha256'=>'b'*64,
  'bootloader_sha256'=>'d'*64,'boot_sha256'=>'c'*64,'recovery_sha256'=>'e'*64,
  'original_env_sha256'=>sha.call(original),'unlock_env_sha256'=>'6'*64,'mutable_sha256'=>'7'*64,
  'protected_env_sha256'=>'8'*64,'bundle_sha256'=>A133Install.bundle_digest(receipt)}
prepare=lambda do |dir|
  A133Cable::Journal.open(dir,cable) do |s|
    s.record('unlocked',write:true,reboot:true,unlocked:true,hardware_bytes:4194304)
    s.record('protecting',protection:true);s.record('verified')
  end
  A133Install::Store.open(File.join(dir,'images'),image_context) { |s| %w[root boot resources].each { |role| s.record(role,'verified') } }
end
Dir.mktmpdir('a133-native-postboot-') do |base|
  # Each test has an independent existing verified cable/image session.
  %w[accepted refused marker epoch ambiguous images bundle missing marker_resume confirm_resume kernel_context].each do |name|
    dir=File.join(base,name);prepare.call(dir)
    src=NativeFixture.new(rearmed,'4'*48)
    params={directory:dir,policy:policy,source:src,nonce:'4'*48,acceptance:-> { src.accept }}
    case name
    when 'accepted'
      result=A133Cable.native_postboot(**params)
      abort 'FAIL: confirmation did not preserve recovery' unless result[:native_confirmed] && !result[:normal_boot_released] && !result[:installation_ready] && src.confirmations==1 && src.writes==0
      result=A133Cable.native_postboot(**params,release:true)
      abort 'FAIL: normal release mismatch' unless result[:normal_boot_released] && src.environment==policy.normal_boot_env(rearmed) && src.writes==1
      A133Cable.native_postboot(**params,release:true)
      abort 'FAIL: repeat released operation wrote again' unless src.writes==1 && src.confirmations==1
      checks+=1
    when 'refused'
      src.accept=false
      fail_case.call('native_acceptance_required') { A133Cable.native_postboot(**params,release:true) }
      abort 'FAIL: refused acceptance changed marker/env' unless src.marker=='4'*48 && src.writes==0 && src.confirmations==0
    when 'marker'
      src.marker='9'*48
      fail_case.call('native_confirmation_marker_changed') { A133Cable.native_postboot(**params) }
    when 'epoch'
      A133Cable.native_postboot(**params)
      src.epoch='other-boot'
      fail_case.call('native_boot_epoch_changed') { A133Cable.native_postboot(**params,release:true) }
    when 'ambiguous'
      src.fail_write=true
      fail_case.call('native_connection_lost') { A133Cable.native_postboot(**params,release:true) }
      src.fail_write=false
      result=A133Cable.native_postboot(**params,release:true)
      abort 'FAIL: ambiguous write was replayed' unless result[:normal_boot_released] && src.writes==1
      checks+=1
    when 'images'
      A133Install::Store.open(File.join(dir,'images'),nil) { |s| s.record('root','pending') }
      fail_case.call('native_images_not_verified') { A133Cable.native_postboot(**params) }
      abort 'FAIL: invalid images reached device' unless src.observations==0
    when 'bundle'
      altered=image_context.merge('source_commit'=>'0'*40)
      file=File.join(dir,'images','state.json');state=JSON.parse(File.read(file));state['context']=altered
      state['checksum']=A133Install.digest(state.reject { |key,_| key=='checksum' });File.write(file,JSON.generate(state))
      fail_case.call('native_bundle_changed') { A133Cable.native_postboot(**params) }
      abort 'FAIL: invalid release reached device' unless src.observations==0
    when 'missing'
      src.marker=nil
      fail_case.call('native_confirmation_marker_changed') { A133Cable.native_postboot(**params) }
    when 'marker_resume'
      src.fail_confirm=true
      fail_case.call('native_connection_lost') { A133Cable.native_postboot(**params) }
      src.fail_confirm=false;src.marker='9'*48
      fail_case.call('native_confirmation_marker_changed') { A133Cable.native_postboot(**params,release:true) }
      abort 'FAIL: foreign resumed marker caused writes' unless src.confirmations==0 && src.writes==0
    when 'confirm_resume'
      src.fail_confirm_after=true
      fail_case.call('native_connection_lost') { A133Cable.native_postboot(**params) }
      src.fail_confirm_after=false
      result=A133Cable.native_postboot(**params,release:true)
      abort 'FAIL: ambiguous confirmation was replayed' unless result[:normal_boot_released] && src.confirmations==1 && src.writes==1
      checks+=1
    when 'kernel_context'
      A133Cable.native_postboot(**params)
      src.instance_variable_set(:@kernel_version_sha256,'0'*64)
      fail_case.call('native_confirmation_context_changed') { A133Cable.native_postboot(**params,release:true) }
      abort 'FAIL: changed running-kernel binding caused a write' unless src.writes==0
    end
  end
end
puts "Native postboot: #{checks} acceptance, binding and interrupted-write cases passed"
