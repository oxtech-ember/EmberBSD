#!/usr/bin/env ruby
# Origin: EmberBSD - strict test-only read-only ADB boundary with real dd source files.
require 'json'
require 'shellwords'
base = File.dirname(__FILE__)
state = JSON.parse(File.read(File.join(base,'state.json')))
serial = 'CAPTURE_TEST_SERIAL'
if ARGV==['devices','-l']
  puts 'List of devices attached'
  row = "#{serial} #{state.fetch('state','device')} #{state['network'] ? '' : 'usb:fixture'}"
  puts row; puts row if state['duplicate']; exit
end
abort 'fixture rejected selected argv' unless ARGV.shift(2)==['-s',serial]
if ARGV==['features']
  puts state.fetch('features','shell_v2'); exit
end
abort 'fixture requires one non-PTY command' unless ARGV.shift(2)==['shell','-T'] && ARGV.size==1
command = ARGV.first
if command.start_with?('exec /system/xbin/su ')
  tokens = Shellwords.split(command)
  abort 'fixture rejected root wrapper' unless tokens.first(5)==['exec','/system/xbin/su','0','/system/bin/sh','-c'] && tokens.size==6
  command = tokens.last
end
case command
when 'id -u' then puts state.fetch('root','0')
when 'getprop ro.product.model' then puts state.fetch('model','a133')
when 'getprop ro.boot.flash.locked' then puts state.fetch('locked',state.fetch('state','device')=='device' ? '1' : '0')
when 'getprop ro.boot.verifiedbootstate' then puts state.fetch('verified',state.fetch('state','device')=='device' ? 'green' : 'orange')
when 'cat /proc/device-tree/compatible' then STDOUT.write("allwinner,a133\0fixture,board\0")
when 'cat /proc/self/mountinfo' then STDOUT.write(state.fetch('mountinfo',"1 0 0:1 / / rw - rootfs rootfs rw\n"))
when 'cat /sys/class/block/mmcblk0/size /sys/class/block/mmcblk0/device/cid /sys/class/block/mmcblk0p1/start /sys/class/block/mmcblk0p1/size /sys/class/block/mmcblk0p2/start /sys/class/block/mmcblk0p2/size /sys/class/block/mmcblk0p3/start /sys/class/block/mmcblk0p3/size /sys/class/block/mmcblk0p4/start /sys/class/block/mmcblk0p4/size /sys/class/block/mmcblk0boot0/size /sys/class/block/mmcblk0boot1/size'
  puts(state['numbers_bad'] ? 1 : 512)
  puts state.fetch('cid','0123456789abcdef0123456789abcdef')
  puts "34\n16\n50\n16\n66\n16\n82\n16\n2"
  puts(state['hardware_bad'] ? 0 : 2)
when 'for p in bootloader env boot recovery; do readlink -f /dev/block/by-name/$p || exit 1; done'
  puts '/dev/block/mmcblk0p1'; puts '/dev/block/mmcblk0p2'; puts '/dev/block/mmcblk0p3'
  puts(state['mapping_bad'] ? '/dev/block/mmcblk0p3' : '/dev/block/mmcblk0p4')
else
  if (match=command.match(%r{\Afor d in ([0-9: ]+); do if \[ -e /sys/dev/block/\$d \]; then echo \$d; fi; done\z}))
    match[1].split.each { |id| puts id if state.fetch('block_ids',[]).include?(id) }; exit
  end
  match = command.match(%r{\Aexec dd if=/dev/block/(mmcblk0(?:p[1-4]|boot[01])?) bs=(512|1048576) (?:skip=([0-9]+) )?count=([0-9]+) 2>/dev/null\z})
  abort 'fixture rejected any write/unexpected command' unless match
  files = {'mmcblk0'=>'disk.raw','mmcblk0p1'=>'bootloader.bin','mmcblk0p2'=>'env.bin',
    'mmcblk0p3'=>'boot.bin','mmcblk0p4'=>'recovery.bin','mmcblk0boot0'=>'boot0.bin','mmcblk0boot1'=>'boot1.bin'}
  file = File.join(base,files.fetch(match[1]))
  whole = match[1]=='mmcblk0' && match[2]=='1048576'
  if whole
    case state['mode']
    when 'short' then STDOUT.write(File.binread(file).byteslice(0,262144-512)); exit
    when 'remote_error' then STDERR.write('PRIVATE_DEVICE_DIAGNOSTIC'); exit 2
    when 'stall' then sleep 10
    when 'noisy' then STDERR.write('PRIVATE_DEVICE_DIAGNOSTIC')
    end
  end
  args = ['dd','if='+file,'bs='+match[2],'count='+match[4]]
  args << 'skip='+match[3] if match[3]
  ok = system(*args,err:File::NULL)
  exit 2 unless ok
  if whole
    case state['mode']
    when 'oversized' then STDOUT.write('x')
    when 'cid_drift'
      state['cid']='f'*32; File.write(File.join(base,'state.json'),JSON.generate(state))
    when 'copy_drift' then File.binwrite(File.join(base,'boot.bin'),'x'*8192)
    end
  end
end
