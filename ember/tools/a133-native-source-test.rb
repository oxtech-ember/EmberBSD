#!/usr/bin/env ruby
# Origin: EmberBSD - pinned SSH options and native read-only observation boundary.
require 'tmpdir'
require 'rbconfig'
require 'base64'
require 'json'
require 'digest'
require 'zlib'
path=File.join(__dir__,'a133-native-source.rb')
abort 'FAIL: native SSH source is missing' unless File.file?(path)
require path
require_relative 'a133-native-postboot'
require_relative 'a133-usb-test-support'
key=Base64.strict_encode64([11].pack('N')+'ssh-ed25519'+[32].pack('N')+'k'*32)
Dir.mktmpdir('a133-native-source-') do |dir|
  known=File.join(dir,'known hosts');File.write(known,"test-alias ssh-ed25519 #{key}\n");File.chmod(0600,known)
  identity=File.join(dir,'identity');File.write(identity,'PRIVATE_TEST_ONLY');File.chmod(0600,identity)
  ssh=File.join(dir,'ssh');log=File.join(dir,'calls')
  File.write(ssh,<<~SCRIPT)
    #!#{RbConfig.ruby}
    require 'json'
    File.open(#{log.inspect},'a',0600) { |f| f.puts JSON.generate(ARGV) }
    command=ARGV.last
    if command.end_with?('sysctl -n kern.boottime')
      puts 'fixture epoch';exit
    end
    abort 'unplanned native command'
  SCRIPT
  File.chmod(0700,ssh)
  client=A133Native::Source.new(ssh:ssh,host:'fe80::1234%en7',host_alias:'test-alias',
    known_hosts:known,identity:identity,host_key:'ssh-ed25519 '+key,kernel_version:'fixture kernel',timeout:5)
  abort 'FAIL: host-key digest mismatch' unless client.host_key_sha256==Digest::SHA256.hexdigest(Base64.strict_decode64(key))
  abort 'FAIL: native epoch read' unless client.epoch=='fixture epoch'
  args=JSON.parse(File.read(log))
  expected=['-F','/dev/null','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes',
    '-o','GlobalKnownHostsFile=/dev/null','-o',"UserKnownHostsFile=\"#{known}\"",'-o','HostKeyAlias=test-alias',
    '-o','IdentitiesOnly=yes','-o','ConnectTimeout=5','-o','ConnectionAttempts=1','-o','UpdateHostKeys=no',
    '-i',identity,'root@fe80::1234%en7']
  abort 'FAIL: unsafe SSH arguments' unless args[0...-1]==expected
  abort 'FAIL: epoch did not check the selected running kernel' unless args.last.include?('test "$(uname -v)" = fixture\\ kernel; ')
  # Exercise the actual observer with independent GPT and binary responses.
  entries=UsbFixture::PARTS.each_with_index.map do |(name,start,size),index|
    'T'*16+[index+1].pack('V').ljust(16,'G')+[start,start+size-1,0].pack('Q<3')+name.encode('UTF-16LE').b.ljust(72,"\0")
  end.join
  mbr="\0".b*512
  mbr[446,16]=[0,0,0,0,0xee,0,0,0].pack('C8')+[1,60620799].pack('V2')
  mbr[510,2]="\x55\xaa".b
  head=(mbr+UsbFixture.header(1,60620799,2,Zlib.crc32(entries))+entries).ljust(17408,"\0")
  tail="\0".b*17408
  tail[28*512,entries.bytesize]=entries
  tail[-512,512]=UsbFixture.header(60620799,1,60620794,Zlib.crc32(entries))
  body=(A133Recovery::PROFILE.merge('opaque'=>"\xff".b).map { |k,v| k.b+'='+v.b+"\0" }.join+"\0").ljust(131068,"\xff".b)
  original=[Zlib.crc32(body)].pack('V')+body+"\xa5".b*(16777216-131072)
  backup={'serial'=>'SOURCE_TEST','cid'=>'1'*32,'status'=>'backup_integrity_verified',
    'bytes'=>A133Backup::BYTES,'uncompressed_sha256'=>'a'*64,'gpt_sha256'=>Digest::SHA256.hexdigest(head+tail),
    'gpt_headers_crc'=>true,'gpt_arrays_crc'=>true,'partition_layout_verified'=>true,
    'critical_copies_verified'=>true,'hardware_boot_copies_verified'=>true,
    'partition_sha256'=>{'boot'=>'b'*64,'bootloader'=>'c'*64,'recovery'=>'d'*64,'env'=>Digest::SHA256.hexdigest(original)}}
  policy=A133Recovery::Policy.new(serial:'SOURCE_TEST',cid:'1'*32,original_env:original,backup:backup)
  nonce='4'*48
  queries={
    'mount'=>"/dev/dk16 on / type ffs (local)\n",
    "test -c /dev/rld0c; test ! -L /dev/rld0c"=>'',
    "stat -f '%Hr:%Lr' /dev/rld0c"=>"92:2\n",
    'dd if=/dev/rld0c bs=512 skip=0 count=34 2>/dev/null'=>head,
    'dd if=/dev/rld0c bs=512 skip=60620766 count=34 2>/dev/null'=>tail,
    'dd if=/dev/rdk1 bs=512 skip=0 count=32768 2>/dev/null'=>original,
    'sha256 -q /dev/rdk2'=>"#{'b'*64}\n",'sha256 -q /dev/rdk5'=>"#{'d'*64}\n"
  }
  [['env',1,32768,139264],['boot',2,65536,172032],['recovery',5,65536,4464640]].each do |name,index,count,start|
    queries["dkctl dk#{index} getwedgeinfo"]="dk#{index} at ld0: #{name}\ndk#{index}: #{count} blocks at #{start}, type: ntfs\n"
    queries["test -c /dev/rdk#{index}; test ! -L /dev/rdk#{index}"]=''
    queries["stat -f '%Hr:%Lr' /dev/rdk#{index}"]="168:#{index}\n"
  end
  marker=nil
  limits=[]
  client.define_singleton_method(:remote) do |command,**options|
    limits << [command,options[:limit]]
    if command.end_with?('sysctl -n kern.boottime')
      "fixture epoch\n"
    elsif command.end_with?('cat /etc/ember/firstboot-pending')
      marker ? marker+"\n" : ''.b
    else
      queries.fetch(command).dup
    end
  end
  observed=client.observe(policy:policy,boot_sha256:'b'*64,nonce:nonce)
  abort 'FAIL: absent marker or wrong epoch in native observer' unless observed=={epoch:'fixture epoch',marker:nil}
  marker=nonce
  abort 'FAIL: observer lost bound marker' unless client.observe(policy:policy,boot_sha256:'b'*64,nonce:nonce)[:marker]==nonce
  abort 'FAIL: native environment binary changed' unless client.environment==original
  abort 'FAIL: native binary bounds missing' unless limits.include?(['dd if=/dev/rdk1 bs=512 skip=0 count=32768 2>/dev/null',16777216])
  refuse=lambda do |reason,&operation|
    begin
      operation.call;abort 'FAIL: observer accepted invalid data'
    rescue A133Cable::Invalid=>error
      abort 'FAIL: wrong observer refusal '+error.message unless error.message==reason && error.cause.nil?
    end
  end
  marker='9'*48
  refuse.call('native_confirmation_marker_changed') { client.observe(policy:policy,boot_sha256:'b'*64,nonce:nonce) }
  marker=nonce
  refuse.call('native_boot_image_changed') { client.observe(policy:policy,boot_sha256:'f'*64,nonce:nonce) }
  queries['dd if=/dev/rdk1 bs=512 skip=0 count=32768 2>/dev/null']='short'
  refuse.call('native_short_read') { client.environment }
  # Restore the concrete transport before testing a changed known_hosts file.
  client.singleton_class.send(:remove_method,:remote)
  before=File.binread(log)
  File.write(known,"test-alias ssh-ed25519 #{Base64.strict_encode64([11].pack('N')+'ssh-ed25519'+[32].pack('N')+'x'*32)}\n")
  begin
    client.epoch;abort 'FAIL: changed known host accepted'
  rescue A133Cable::Invalid=>error
    abort 'FAIL: wrong host refusal' unless error.message=='native_host_key_changed' && error.cause.nil?
  end
  abort 'FAIL: changed key reached SSH' unless File.binread(log)==before
end
puts 'Native SSH source: pinned key, argv, GPT/marker observer, exact binary bounds and changed-key refusal passed'
