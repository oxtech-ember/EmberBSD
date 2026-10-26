#!/usr/bin/env ruby
# Origin: EmberBSD - bounded direct factory saveenv export and real restore.
require 'tmpdir'
require_relative 'a133-recovery-reboot-test-support'
require_relative 'a133-recovery-entry'
checks=0
encode=lambda do |values|
  body=values.map {|k,v|k.b+'='+v.b+"\0"}.join+"\0"
  body=body.ljust(131068,"\xff".b);[Zlib.crc32(body)].pack('V')+body
end
Dir.mktmpdir('runtime-env-') do |dir|
  context=RebootFixture.create(dir,direct:true)
  original_values=A133Env.decode(context[:original].byteslice(0,131072))[:entries].to_h.merge('mac'=>'','wifi_mac'=>'','bt_mac'=>'','specialstr'=>'')
  original=encode.call(original_values)+context[:original].byteslice(131072..)
  backup=context[:backup].merge('partition_sha256'=>context[:backup]['partition_sha256'].merge('env'=>Digest::SHA256.hexdigest(original)))
  policy=A133Recovery::Policy.new(serial:RebootFixture::SERIAL,cid:UsbFixture::CID,original_env:original,backup:backup)
  export=policy.data[:consumed].reject {|k,_|%w[wifi_mac bt_mac specialstr].include?(k)}.merge(
    'mac'=>'02:12:34:56:78:90','snum'=>'0011223344556677889','bootreason'=>'unknow',
    'fdtcontroladdr'=>'7beb3280','fileaddr'=>'40000000','filesize'=>'2a338','rotpk_status'=>'0',
    'partitions'=>A133Backup::INVENTORY.map {|p|"#{p[:name]}@mmcblk0p#{p[:index]}"}.join(':'))
  current=lambda {|v|{prefix:encode.call(v),tail_sha:policy.data[:tail_sha]}}
  abort 'FAIL: measured factory export refused' unless policy.matches?('consumed',current.call(export));checks+=1
  mutations=[['bootcmd','run boot_recovery'],['snum','FOREIGN'],['mac','invalid'],['rotpk_status','1'],['fileaddr','50000000'],['filesize','1'],['fdtcontroladdr','7beb3288'],['bootreason','normal'],['partitions','bad'],['wifi_mac',''],['unknown','value']]
  mutations.each do |k,v|
    abort 'FAIL: foreign export change accepted' if policy.matches?('consumed',current.call(export.merge(k=>v)));checks+=1
  end
  %w[mac snum bootreason fdtcontroladdr fileaddr filesize rotpk_status partitions].each do |k|
    abort 'FAIL: partial export accepted' if policy.matches?('consumed',current.call(export.reject {|key,_|key==k}));checks+=1
  end
  abort 'FAIL: tail drift accepted' if policy.matches?('consumed',current.call(export).merge(tail_sha:'f'*64));checks+=1
  File.write(File.join(dir,'adb-source.rb'),File.read(File.join(__dir__,'a133-recovery-protection-fixture.rb')))
  File.write(File.join(dir,'adb'),File.read(File.join(__dir__,'a133-recovery-reboot-fixture.rb')))
  live=encode.call(export)+original.byteslice(131072..)
  RebootFixture.reset(dir,context,{'state'=>'recovery'},live)
  result=A133Recovery::Entry.new(adb:File.join(dir,'adb'),serial:RebootFixture::SERIAL,cid:UsbFixture::CID,state:'recovery',root_method:'adbd',timeout:5).restore(original_env:original,backup:backup)
  abort 'FAIL: measured export did not restore exact full env' unless result[:writes_performed]==1 && File.binread(File.join(dir,'part2'))==original
  checks+=1
end
puts "Factory runtime env: #{checks} exact export, rejection and actual-dd restore cases passed"
