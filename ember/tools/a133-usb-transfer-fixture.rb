#!/usr/bin/env ruby
# Origin: EmberBSD - test-only strict ADB boundary executing real file-backed dd.
require 'json'
require 'shellwords'
base = File.dirname(__FILE__)
state = JSON.parse(File.read(File.join(base,'state.json')))
parts = [
  ['bootloader',73728,65536], ['env',139264,32768], ['boot',172032,65536],
  ['super',237568,4194304], ['misc',4431872,32768], ['recovery',4464640,65536],
  ['cache',4530176,1572864], ['vbmeta',6103040,32768], ['vbmeta_system',6135808,32768],
  ['vbmeta_vendor',6168576,32768], ['metadata',6201344,32768], ['private',6234112,32768],
  ['frp',6266880,1024], ['empty',6267904,31744], ['dtbo',6299648,4096],
  ['media_data',6303744,262144], ['UDISK',6565888,54054879]
]
if ARGV == ['devices','-l']
  usb = state['network'] ? '' : 'usb:fixture'
  line = "USB_SAMPLE #{state.fetch('state','recovery')} #{usb}"
  puts "List of devices attached\n#{line}"
  puts line if state['duplicate']
  exit
end
abort 'fixture rejected device/argv' unless ARGV.shift(2) == ['-s','USB_SAMPLE']
if ARGV == ['features']
  puts state.fetch('features','shell_v2'); exit
end
abort 'fixture requires non-PTY shell' unless ARGV.shift(2) == ['shell','-T'] && ARGV.size == 1
command = ARGV.first
case command
when 'id -u' then puts state.fetch('root','0')
when 'getprop ro.product.model' then puts state.fetch('model','a133')
when 'getprop ro.boot.flash.locked' then puts state.fetch('locked','0')
when 'getprop ro.boot.verifiedbootstate' then puts state.fetch('verified','orange')
when 'cat /proc/device-tree/compatible' then STDOUT.write(state.fetch('compatible',"allwinner,a133\0vendor,board\0"))
when 'cat /proc/mounts' then STDOUT.write(state.fetch('mounts',"tmpfs /tmp tmpfs rw 0 0\n"))
when 'cat /proc/self/mountinfo' then STDOUT.write(state.fetch('mountinfo',"1 0 0:1 / / rw - rootfs rootfs rw\n"))
when 'cat /sys/class/block/mmcblk0p1/dev /sys/class/block/mmcblk0p2/dev /sys/class/block/mmcblk0p3/dev /sys/class/block/mmcblk0p17/dev'
  puts "179:1\n179:2\n179:3\n179:17"
else
  paths = ['/sys/class/block/mmcblk0/size','/sys/class/block/mmcblk0/device/cid'] +
    (1..17).flat_map { |i| ["/sys/class/block/mmcblk0p#{i}/start","/sys/class/block/mmcblk0p#{i}/size"] }
  mapping = "for p in #{parts.map(&:first).join(' ')}; do readlink -f /dev/block/by-name/$p || exit 1; done"
  if command == 'cat ' + paths.join(' ')
    puts 60620800
    puts state.fetch('cid','0123456789abcdef0123456789abcdef')
    parts.each_with_index { |(_,start,size),i| puts(start); puts(state['numbers_bad'] && i == 16 ? 1 : size) }
  elsif command == mapping
    (1..17).each { |i| puts "/dev/block/mmcblk0p#{state['mapping_bad'] && i == 17 ? 3 : i}" }
  elsif (match = command.match(%r{\Afor d in ([0-9: ]+); do if \[ -e /sys/dev/block/\$d \]; then echo \$d; fi; done\z}))
    match[1].split.each { |id| puts id if state.fetch('block_ids',[]).include?(id) }
  elsif (match = command.match(%r{\Aexec dd if=/dev/block/mmcblk0(p[0-9]+)? bs=512 skip=([0-9]+) count=([0-9]+) 2>/dev/null\z}))
    part = match[1] ? "part#{match[1][1..]}" : 'disk'
    file = File.join(base,part)
    if part == 'part17' && state['mode'] == 'short_read' && File.exist?(File.join(base,'written'))
      File.open(file,'rb') { |input| STDOUT.write(input.read(256)) }; exit
    end
    exec('sh','-c',"exec dd if=#{Shellwords.escape(file)} bs=512 skip=#{match[2]} count=#{match[3]} 2>/dev/null")
  elsif (match = command.match(%r{\Add of=/dev/block/mmcblk0p(1|3|17) bs=1048576 conv=notrunc 2>/dev/null && sync\z}))
    target = File.join(base,"part#{match[1]}")
    File.write(File.join(base,'written'),'attempted')
    if state['mode'] == 'partial'
      exec('sh','-c',"dd of=#{Shellwords.escape(target)} bs=512 count=1 conv=notrunc 2>/dev/null; exit 2")
    end
    ok = system('sh','-c',"dd of=#{Shellwords.escape(target)} bs=1048576 conv=notrunc 2>/dev/null && sync")
    exit 2 unless ok
    File.open(target,'r+b') { |file| file.write('X') } if state['mode'] == 'corrupt'
    File.utime(Time.now+10,Time.now+10,File.join(base,'source')) if state['mode'] == 'source_changed'
    exit 3 if state['mode'] == 'sync_failed'
  else
    abort 'fixture rejected unexpected command'
  end
end
