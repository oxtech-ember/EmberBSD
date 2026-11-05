#!/usr/bin/env ruby
# Origin: EmberBSD - one recovery MCU action through the checked ADB channel.
require 'tmpdir'
require 'fileutils'
require 'shellwords'
require_relative 'a133-recovery-mcu'

Dir.mktmpdir('a133-mcu') do |dir|
  serial='fixture-usb'; cid='ab'*16; epoch='01234567-89ab-4def-8765-0123456789ab'
  File.write("#{dir}/cid",cid+"\n")
  File.write("#{dir}/epoch",epoch+"\n")
  FileUtils.mkdir("#{dir}/ram")
  File.chmod(0775,"#{dir}/ram")
  # Only the platform queries are modelled. The ADB channel and remote shell run.
  fake=<<~'TOOL'
    require 'digest'
    dir=File.dirname(__FILE__)
    case File.basename($0)
    when 'adb'
      File.open("#{dir}/adb.calls",'a') { |f| f.puts ARGV.first }
      if ARGV==%w[devices -l]
        puts "fixture-usb recovery usb:fixture"
      elsif ARGV==%w[-s fixture-usb features]
        puts 'shell_v2'
      elsif ARGV.take(4)==%w[-s fixture-usb shell -T]
        script=ARGV.fetch(4).gsub('/tmp',dir+'/ram')
        {'/sys/class/block/mmcblk0/device/cid'=>'cid','/proc/sys/kernel/random/boot_id'=>'epoch'}.each { |key,value| script=script.gsub(key,dir+'/'+value) }
        exec({'PATH'=>dir+':'+ENV.fetch('PATH')},'/bin/sh','-c',script)
      else
        exit 2
      end
    when 'id' then puts '0'
    when 'getprop' then puts({'ro.product.model'=>'QUAD-CORE A133 c3','ro.boot.flash.locked'=>'0',
                              'ro.boot.verifiedbootstate'=>'orange'}.fetch(ARGV.fetch(0)))
    when 'stat'
      if ARGV==['-f','-c','%t','.']
        puts '1021994'
      elsif ARGV==['-c','%u:%a','.']
        puts "0:#{(File.stat('.').mode & 07777).to_s(8)}"
      elsif ARGV.first(2)==['-c','%d:%i:%u:%a']
        entry=File.lstat(ARGV.fetch(2))
        puts "#{entry.dev}:#{entry.ino}:0:#{(entry.mode & 07777).to_s(8)}"
      else
        exit 1
      end
    when 'sha256sum'
      hash=File.exist?("#{dir}/corrupt-readback") ? '0'*64 : Digest::SHA256.file(ARGV.fetch(0)).hexdigest
      puts "#{hash}  #{ARGV.fetch(0)}"
    end
  TOOL
  path="#{dir}/fixture-tool"
  File.write(path,"#!#{RbConfig.ruby}\n"+fake); File.chmod(0700,path)
  %w[adb id getprop stat sha256sum].each { |name| File.symlink(path,"#{dir}/#{name}") }
  adb="#{dir}/adb"
  mcu=A133Recovery::McuControl.new(adb:adb,serial:serial,cid:cid,model:'QUAD-CORE A133 c3',timeout:5)
  calls="#{dir}/executions"
  count=lambda { File.exist?(calls) ? File.readlines(calls).length : 0 }
  run=lambda do |action,words,options={}|
    helper="#{dir}/local-helper"
    bytes=words.pack('Q<*').bytes.map { |byte| format('\\%03o',byte) }.join
    File.write(helper,"#!/bin/sh\necho invoked >> #{Shellwords.escape(calls)}\n"+
      (options[:change] ? "echo changed > #{Shellwords.escape(dir+'/epoch')}\n" : '')+
      "printf '#{bytes}'\nexit #{options.fetch(:exit,0)}\n")
    sha=options[:wrong_sha] ? '0'*64 : Digest::SHA256.file(helper).hexdigest
    mcu.run(action:action,helper:helper,sha256:sha,boot_id:epoch)
  end
  reject=lambda do |attempted,&work|
    begin; work.call
    rescue A133Recovery::McuControl::Invalid => e
      raise 'incorrect dispatch outcome' unless e.action_attempted==attempted
    else; raise 'unsafe acceptance'; end
  end
  feed=[0x170307,128,7,0,0,255,0xc360]; arm=feed.dup; arm[3,2]=[7,7]
  begin
    mcu.run(action:'feed',helper:"#{dir}/missing-private-helper",sha256:'0'*64,boot_id:epoch)
    raise 'missing helper accepted'
  rescue A133Recovery::McuControl::Invalid => e
    raise 'private path retained in exception' unless e.message=='mcu_io_failed' &&
      e.cause.nil? && !e.action_attempted
  end
  adb.replace("#{dir}/untrusted-adb")
  reject.call(false) { run.call('feed',feed,wrong_sha:true) }
  raise 'wrong hash reached transport' if File.exist?("#{dir}/adb.calls")
  staging="#{dir}/ram/ember-mcu7502"
  FileUtils.mkdir(staging)
  File.chmod(0775,staging)
  reject.call(true) { run.call('feed',feed) }
  Dir.rmdir(staging)
  FileUtils.mkdir("#{dir}/other",mode:0700)
  File.symlink("#{dir}/other",staging)
  reject.call(true) { run.call('feed',feed) }
  File.unlink(staging)
  raise 'unsafe RAM directory used' unless count.call==0 && Dir.empty?("#{dir}/other")
  File.write("#{dir}/corrupt-readback",'1')
  reject.call(true) { run.call('feed',feed) }
  File.unlink("#{dir}/corrupt-readback")
  raise 'unverified helper executed' unless count.call==0
  reject.call(true) { run.call('feed',feed.take(6)) }
  reject.call(true) { run.call('feed',arm) }
  reject.call(true) { run.call('arm',feed) }
  reject.call(true) { run.call('feed',feed,exit:9) }
  reject.call(true) { run.call('feed',feed,change:true) }
  raise 'ambiguous action replayed or skipped' unless count.call==5
  # A new transaction with the stale expected epoch cannot reach the helper.
  reject.call(true) { run.call('feed',feed) }
  raise 'stale epoch executed' unless count.call==5
  File.write("#{dir}/epoch",epoch+"\n")
  {'arm'=>arm,'feed'=>feed,'disable'=>feed}.each do |action,ack|
    raise 'missing acknowledgement' unless run.call(action,ack)==true
  end
  # File.open asks for the path after run has bound the action. Change only
  # the caller's strings at that boundary, before any ADB subprocess starts.
  local="#{dir}/local-helper"
  action='feed'.dup; digest=Digest::SHA256.file(local).hexdigest; boot=epoch.dup
  input=Object.new
  input.define_singleton_method(:to_path) do
    action.replace('arm'); digest.replace('0'*64); boot.replace('changed')
    local
  end
  raise 'caller mutation changed dispatch' unless mcu.run(action:action,helper:input,sha256:digest,boot_id:boot)
  raise 'wrong execution count' unless count.call==9
end
puts 'PASS: guarded recovery MCU actions and ambiguous delivery'
