#!/usr/bin/env ruby
# Origin: EmberBSD - actual-dd unlock boundary with independently encoded vendor return.
require 'json'
require 'shellwords'
base=File.dirname(__FILE__)
state_path=File.join(base,'state.json')
state=JSON.parse(File.read(state_path))
serial='PROTECTION_TEST_SERIAL'
if ARGV==['devices','-l']
  File.write(File.join(base,'observed'),'1')
  puts 'List of devices attached'
  unless state['missing']
    transport=state['tcp'] ? 'product:fixture' : 'usb:fixture'
    puts "#{serial} #{state.fetch('state','device')} #{transport}"
    puts "#{serial} #{state.fetch('state','device')} #{transport}" if state['duplicate']
  end
  exit
end
command=ARGV.last.to_s
command=Shellwords.split(command).last if command.start_with?('exec /system/xbin/su ')
write=command=='dd of=/dev/block/mmcblk0p2 bs=1048576 conv=notrunc 2>/dev/null && sync'
reboot=ARGV==['-s',serial,'reboot']
if write || reboot
  journal=JSON.parse(File.read(state.fetch('journal_path')))
  abort 'fixture rejected missing durable intent' unless journal['possible_write'] &&
    (reboot ? journal['possible_reboot'] && journal['phase']=='rebooting' : %w[arming restoring].include?(journal['phase']))
  File.open(File.join(base,write ? 'write-count' : 'reboot-count'),'a') { |f| f.puts journal['phase'] }
end
if reboot
  File.open(File.join(base,'part2'),'r+b') { |f| f.write(File.binread(File.join(base,'unlock-consumed.bin'))) }
  state.merge!('state'=>'recovery','locked'=>'0','verified'=>'orange')
  state.merge!(state.fetch('after_reboot',{}))
  if state['corrupt_after_reboot']
    File.open(File.join(base,'part6'),'r+b') { |f| f.write('X') }
  end
  File.write(state_path,JSON.generate(state))
  if state['reboot_failure']
    STDERR.write('PRIVATE_REBOOT_DIAGNOSTIC'); exit 2
  end
  exit
end
load File.join(base,'adb-source.rb')
