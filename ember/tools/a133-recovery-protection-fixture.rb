#!/usr/bin/env ruby
# Origin: EmberBSD - strict recovery protection transport fixture with actual dd.
require 'json'
require 'shellwords'
base=File.dirname(__FILE__)
state_path=File.join(base,'state.json')
state=JSON.parse(File.read(state_path))
serial='PROTECTION_TEST_SERIAL'
File.write(File.join(base,'observed'),'1')
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
when 'getprop ro.boot.flash.locked' then puts state.fetch('locked','1')
when 'getprop ro.boot.verifiedbootstate' then puts state.fetch('verified','green')
when 'cat /proc/device-tree/compatible' then STDOUT.write("allwinner,a133\0fixture,board\0")
when 'cat /proc/self/mountinfo' then STDOUT.write("1 0 0:1 / / rw - rootfs rootfs rw\n")
when 'for p in /proc/[0-9]*; do for t in "$p"/task/[0-9]*; do cat "$t/mountinfo" || exit 1; done; done'
  if state['inventory_unreadable'] || state['thread_unreadable']
    STDERR.write('PRIVATE_THREAD_DIAGNOSTIC'); exit 1
  end
  STDOUT.write(state.fetch('mountinfo',"1 0 0:1 / / rw - rootfs rootfs rw\n"))
  STDOUT.write(state.fetch('thread_mountinfo',''))
when 'cat /sys/class/block/mmcblk0/dev /sys/class/block/mmcblk0p2/dev'
  exit 2 if state['usage_unreadable']
  STDOUT.write(state.fetch('dev_ids',"179:0\n179:2\n"))
when 'for p in /sys/class/block/mmcblk0 /sys/class/block/mmcblk0p2; do [ -d "$p/holders" ] && [ -r "$p/holders" ] && [ -x "$p/holders" ] || exit 1; for d in "$p"/holders/*; do if [ -e "$d" ]; then echo "$d"; fi; done; done'
  exit 2 if state['holders_unreadable']
  puts '/sys/class/block/mmcblk0p2/holders/dm-0' if state['holders']
when 'cat /proc/swaps'
  STDOUT.write(state.fetch('swaps',"Filename\tType\tSize\tUsed\tPriority\n"))
when 'dd of=/dev/block/mmcblk0p2 bs=1048576 conv=notrunc 2>/dev/null && sync'
  data=STDIN.read.b
  abort 'fixture rejected incorrect write size' unless data.bytesize==131072
  File.write(File.join(base,'written'),'env')
  if state['write_mode']=='partial'
    File.open(File.join(base,'part2'),'r+b') { |file| file.write(data.byteslice(0,512)) }
    STDERR.write('PRIVATE_WRITE_DIAGNOSTIC'); exit 2
  end
  IO.popen(['dd','of='+File.join(base,'part2'),'bs=1048576','conv=notrunc',:err=>File::NULL],'wb') { |io| io.write(data) }
  exit 2 unless $?.success?
  case state['write_mode']
  when 'sync_failed' then STDERR.write('PRIVATE_SYNC_DIAGNOSTIC'); exit 2
  when 'stall' then sleep 10
  when 'tail_corrupt'
    File.open(File.join(base,'part2'),'r+b') { |f| f.seek(16777215); f.write('X') }
  when 'recovery_corrupt'
    File.open(File.join(base,'part6'),'r+b') { |f| f.write('X') }
  when 'bootloader_corrupt'
    File.open(File.join(base,'part1'),'r+b') { |f| f.write('X') }
  when 'cid_drift' then state['cid']='f'*32
  when 'late_mount'
    state['mountinfo']="1 0 0:1 / / rw - rootfs rootfs rw\n2 1 179:2 / /alias rw - ext4 /dev/block/alias rw\n"
    state['block_ids']=['179:2']
  when 'late_holders_unreadable'
    state['holders_unreadable']=true
  when 'launcher_invalid'
    File.rename(File.join(base,'launch'),File.join(base,'launch-retained'))
    File.write(File.join(base,'launch'),'PRIVATE_HOST_LOCATION')
  end
  File.write(state_path,JSON.generate(state))
else
  if command.start_with?('for p in /sys/class/block/mmcblk0 ')
    parts=JSON.parse(File.read(File.join(base,'layout.json')))
    paths=['/sys/class/block/mmcblk0']+parts.each_index.map { |i| "/sys/class/block/mmcblk0p#{i+1}" }
    expected='for p in '+paths.join(' ')+'; do [ -d "$p/holders" ] && [ -r "$p/holders" ] && [ -x "$p/holders" ] || exit 1; for d in "$p"/holders/*; do if [ -e "$d" ]; then echo "$d"; fi; done; done'
    abort 'fixture rejected incomplete holder directory inventory' unless command==expected
    exit 2 if state['holders_unreadable']
    puts '/sys/class/block/mmcblk0p2/holders/dm-0' if state['holders']
    exit
  end
  if command.start_with?('cat /sys/class/block/mmcblk0/size ')
    parts=JSON.parse(File.read(File.join(base,'layout.json')))
    expected=['/sys/class/block/mmcblk0/size','/sys/class/block/mmcblk0/device/cid']+
      parts.each_index.flat_map { |i| ["/sys/class/block/mmcblk0p#{i+1}/start","/sys/class/block/mmcblk0p#{i+1}/size"] }+
      ['/sys/class/block/mmcblk0boot0/size','/sys/class/block/mmcblk0boot1/size']
    abort 'fixture rejected layout query' unless command=='cat '+expected.join(' ')
    puts state.fetch('size',60620800)
    puts state.fetch('cid','0123456789abcdef0123456789abcdef')
    parts.each { |_,start,size| puts start; puts size }
    puts state.fetch('hardware_sectors',8192); puts state.fetch('hardware_sectors',8192); exit
  end
  if command.start_with?('for p in bootloader ')
    parts=JSON.parse(File.read(File.join(base,'layout.json')))
    abort 'fixture rejected mapping query' unless command=="for p in #{parts.map(&:first).join(' ')}; do readlink -f /dev/block/by-name/$p || exit 1; done"
    parts.each_index { |i| puts "/dev/block/mmcblk0p#{i+1}" }; exit
  end
  if (match=command.match(%r{\Afor d in ([0-9: ]+); do if \[ -e /sys/dev/block/\$d \]; then echo \$d; fi; done\z}))
    match[1].split.each { |id| puts id if state.fetch('block_ids',[]).include?(id) }; exit
  end
  match=command.match(%r{\Aexec dd if=/dev/block/(mmcblk0(?:p[1236])?) bs=(512|1048576) (?:skip=([0-9]+) )?count=([0-9]+) 2>/dev/null\z})
  abort 'fixture rejected unexpected/write command' unless match
  role={'mmcblk0'=>'disk','mmcblk0p1'=>'part1','mmcblk0p2'=>'part2','mmcblk0p3'=>'part3','mmcblk0p6'=>'part6'}.fetch(match[1])
  if role=='part2'
    case state['read_mode']
    when 'short' then STDOUT.write(File.binread(File.join(base,role))[0...-512]); exit
    when 'noisy' then STDERR.write('PRIVATE_READ_DIAGNOSTIC')
    when 'failed' then STDERR.write('PRIVATE_READ_DIAGNOSTIC'); exit 2
    end
  end
  args=['dd','if='+File.join(base,role),'bs='+match[2],'count='+match[4]]
  args << 'skip='+match[3] if match[3]
  exit 2 unless system(*args,err:File::NULL)
  if role=='part2' && state['prewrite_mount']
    state['mountinfo']="1 0 0:1 / / rw - rootfs rootfs rw\n2 1 179:2 / /alias rw - ext4 /dev/block/alias rw\n"
    state['block_ids']=['179:2']; File.write(state_path,JSON.generate(state))
  end
end
