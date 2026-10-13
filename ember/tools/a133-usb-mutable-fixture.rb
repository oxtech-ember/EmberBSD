#!/usr/bin/env ruby
# Origin: EmberBSD - strict mutable read-only ADB fixture using real dd, test processes only.
require 'json'
require 'shellwords'
base=File.dirname(__FILE__)
state_path=File.join(base,'state.json')
state=JSON.parse(File.read(state_path))
serial='MUTABLE_TEST_SERIAL'
if ARGV==['devices','-l']
  puts 'List of devices attached'
  puts "#{serial} #{state.fetch('state','recovery')} usb:fixture"; exit
end
abort 'fixture rejected serial/argv' unless ARGV.shift(2)==['-s',serial]
if ARGV==['features']
  puts 'shell_v2'; exit
end
abort 'fixture requires exactly one non-PTY command' unless ARGV.shift(2)==['shell','-T'] && ARGV.size==1
command=ARGV.first
if command.start_with?('exec /system/xbin/su ')
  tokens=Shellwords.split(command)
  abort 'fixture rejected root wrapper' unless tokens.size==6 && tokens.first(5)==['exec','/system/xbin/su','0','/system/bin/sh','-c']
  command=tokens.last
end
case command
when 'id -u' then puts state.fetch('root','0')
when 'getprop ro.product.model' then puts 'a133'
when 'getprop ro.boot.flash.locked' then puts '0'
when 'getprop ro.boot.verifiedbootstate' then puts 'orange'
when 'cat /proc/device-tree/compatible' then STDOUT.write("allwinner,a133\0fixture,board\0")
when 'cat /proc/self/mountinfo' then STDOUT.write("1 0 0:1 / / rw - rootfs rootfs rw\n")
when 'for p in /proc/[0-9]*; do cat "$p/mountinfo" || exit 1; done',
  'for p in /proc/[0-9]*; do for t in "$p"/task/[0-9]*; do cat "$t/mountinfo" || exit 1; done; done'
  if state['inventory_unreadable']
    STDERR.write('PRIVATE_PROCESS_DIAGNOSTIC'); exit 1
  end
  STDOUT.write(state.fetch('mountinfo',"1 0 0:1 / / rw - rootfs rootfs rw\n"))
  if command.include?('/task/')
    if state['thread_unreadable']
      STDERR.write('PRIVATE_THREAD_DIAGNOSTIC'); exit 1
    end
    STDOUT.write(state.fetch('thread_mountinfo',''))
  end
when 'cat /proc/swaps'
  STDOUT.write(state.fetch('swaps',"Filename\tType\tSize\tUsed\tPriority\n"))
when 'cat /sys/class/block/mmcblk0/size /sys/class/block/mmcblk0/device/cid /sys/class/block/mmcblk0p1/start /sys/class/block/mmcblk0p1/size /sys/class/block/mmcblk0p2/start /sys/class/block/mmcblk0p2/size /sys/class/block/mmcblk0p3/start /sys/class/block/mmcblk0p3/size /sys/class/block/mmcblk0p4/start /sys/class/block/mmcblk0p4/size /sys/class/block/mmcblk0p5/start /sys/class/block/mmcblk0p5/size /sys/class/block/mmcblk0p6/start /sys/class/block/mmcblk0p6/size /sys/class/block/mmcblk0boot0/size /sys/class/block/mmcblk0boot1/size'
  puts "512\n#{state.fetch('cid','0123456789abcdef0123456789abcdef')}\n34\n16\n50\n16\n66\n16\n82\n16\n98\n16\n114\n201\n2\n2"
when 'for p in bootloader env boot recovery metadata UDISK; do readlink -f /dev/block/by-name/$p || exit 1; done'
  (1..6).each { |i| puts '/dev/block/mmcblk0p'+i.to_s }
else
  if command.start_with?('for p in /sys/class/block/mmcblk0 ')
    paths=['/sys/class/block/mmcblk0']+(1..6).map { |i| "/sys/class/block/mmcblk0p#{i}" }
    expected='for p in '+paths.join(' ')+'; do [ -d "$p/holders" ] && [ -r "$p/holders" ] && [ -x "$p/holders" ] || exit 1; for d in "$p"/holders/*; do if [ -e "$d" ]; then echo "$d"; fi; done; done'
    abort 'fixture rejected incomplete holder directory inventory' unless command==expected
    exit 2 if state['holders_unreadable']
    puts '/sys/class/block/mmcblk0p6/holders/dm-0' if state['holders']
    exit
  end
  if (match=command.match(%r{\Afor d in ([0-9: ]+); do if \[ -e /sys/dev/block/\$d \]; then echo \$d; fi; done\z}))
    match[1].split.each { |id| puts id if state.fetch('block_ids',[]).include?(id) }; exit
  end
  match=command.match(%r{\Aexec dd if=/dev/block/(mmcblk0(?:p[56])?) bs=(512|1048576) (?:skip=([0-9]+) )?count=([0-9]+) 2>/dev/null\z})
  abort 'fixture rejected unexpected/write command' unless match
  role={'mmcblk0'=>'disk','mmcblk0p5'=>'metadata','mmcblk0p6'=>'UDISK'}.fetch(match[1])
  file=File.join(base,role.downcase+'.raw')
  if role!='disk'
    state['reads']||={}; state['reads'][role]=state['reads'].fetch(role,0)+1
    File.write(state_path,JSON.generate(state))
    case state['mode']
    when 'short' then STDOUT.write(File.binread(file)[0...-512]); exit
    when 'noisy' then STDERR.write('PRIVATE_READ_DIAGNOSTIC')
    when 'failed' then STDERR.write('PRIVATE_READ_DIAGNOSTIC'); exit 2
    when 'stall' then sleep 10
    end
  end
  args=['dd','if='+file,'bs='+match[2],'count='+match[4]]
  args << 'skip='+match[3] if match[3]
  exit 2 unless system(*args,err:File::NULL)
  if role!='disk'
    if state['mutate']==role && state['reads'][role]==1
      data=File.binread(file); data.setbyte(data.bytesize-1,data.getbyte(data.bytesize-1)^0xff); File.binwrite(file,data)
    end
    if state['drift'] && role=='metadata' && state['reads'][role]==1
      state['cid']='f'*32
    end
    if state['late_mount'] && role=='metadata' && state['reads'][role]==2
      state['mountinfo']="1 0 179:6 / /alias rw - ext4 /dev/block/alias rw\n"; state['block_ids']=['179:6']
    end
    STDOUT.write('x') if state['mode']=='oversized'
    File.write(state_path,JSON.generate(state))
  end
end
