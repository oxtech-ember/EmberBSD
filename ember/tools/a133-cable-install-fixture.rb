#!/usr/bin/env ruby
# Origin: EmberBSD - native combined unlock/protection/image boundary with real dd.
require 'json'
require 'shellwords'
base=File.dirname(__FILE__)
state_path=File.join(base,'state.json')
state=JSON.parse(File.read(state_path))
serial='PROTECTION_TEST_SERIAL'
if ARGV==['devices','-l']
  File.write(File.join(base,'observed'),'1')
  puts "List of devices attached\n#{serial} #{state.fetch('state','device')} usb:fixture"
  exit
end
command=ARGV.last.to_s
if command.start_with?('exec /system/xbin/su ')
  tokens=Shellwords.split(command)
  abort 'fixture rejected root wrapper' unless tokens.size==6 && tokens.first(5)==['exec','/system/xbin/su','0','/system/bin/sh','-c']
  command=tokens.last
end
reboot=ARGV==['-s',serial,'reboot']
envwrite=command=='dd of=/dev/block/mmcblk0p2 bs=1048576 conv=notrunc 2>/dev/null && sync'
imagewrite=command.match?(%r{\Add of=/dev/block/mmcblk0p(1|3|17) })
if reboot || envwrite || imagewrite
  outer=JSON.parse(File.read(state.fetch('outer_path')))
  abort 'fixture rejected missing outer intent' unless outer['possible_write'] &&
    (reboot ? outer['possible_reboot'] : true)
  if outer['protection_started']
    abort 'fixture rejected unlock after protection' if reboot
    abort 'fixture rejected bad protection marker' unless outer['unlock_verified'] &&
      %w[protecting writing].include?(outer['phase'])
  else
    inner=JSON.parse(File.read(state.fetch('unlock_path')))
    abort 'fixture rejected missing inner intent' unless inner['possible_write'] &&
      (reboot ? inner['possible_reboot'] && inner['phase']=='rebooting' : %w[arming restoring].include?(inner['phase']))
  end
  if imagewrite
    inner=JSON.parse(File.read(state.fetch('images_path')))
    role={'17'=>'root','3'=>'boot','1'=>'resources'}.fetch(command[%r{mmcblk0p(\d+)},1])
    abort 'fixture rejected image intent' unless inner['roles'][role]['state']=='writing' && outer['phase']=='writing'
  end
  File.open(File.join(base,'effects'),'a') { |f| f.puts(reboot ? 'reboot' : imagewrite ? command[%r{mmcblk0p\d+}] : outer['protection_started'] ? 'protection' : 'unlock_env') }
end
if reboot
  File.open(File.join(base,'part2'),'r+b') { |f| f.write(File.binread(File.join(base,'unlock-consumed.bin'))) }
  state.merge!('state'=>'recovery','locked'=>'0','verified'=>'orange')
  File.write(state_path,JSON.generate(state)); exit
end
writer=imagewrite || command=='cat /proc/mounts' || command.start_with?('cat /sys/class/block/mmcblk0p1/dev ') ||
  (command.start_with?('cat /sys/class/block/mmcblk0/size ') && !command.include?('mmcblk0boot0')) ||
  command.match?(%r{\Aexec dd if=/dev/block/mmcblk0(?:p\d+)? bs=512 skip=\d+ count=\d+ 2>/dev/null\z})
if writer
  ARGV[1]='USB_SAMPLE'
  ARGV[-1]=command
  load File.join(base,'adb-writer.rb')
else
  load File.join(base,'adb-source.rb')
end
if imagewrite && command.start_with?('dd of=/dev/block/mmcblk0p17 ') && state['after_root']
  changed=JSON.parse(File.read(state_path))
  changed['holders']=true
  changed['mode']='partial'
  File.write(state_path,JSON.generate(changed))
end
