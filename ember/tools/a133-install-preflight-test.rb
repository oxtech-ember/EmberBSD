#!/usr/bin/env ruby
# Origin: EmberBSD - exercise cable installation inspection without a tablet.
require 'tmpdir'
require 'open3'
require 'json'
require 'rbconfig'
src = File.expand_path('../..', __dir__)
tool = File.join(src, 'ember/tools/a133-install-preflight.rb')
abort 'FAIL: read-only cable installation inspector is missing' unless File.file?(tool)
Dir.mktmpdir('a133-preflight-') do |dir|
  adb = File.join(dir, 'adb')
  File.write(adb, <<~'FAKE')
    #!/usr/bin/env ruby
    mode = ENV.fetch('PREFLIGHT_CARD', 'good')
    sleep 5 if mode == 'timeout'
    abort 'fixture transport failure' if mode == 'failure'
    if ARGV == ['devices', '-l']
      puts 'List of devices attached'
      unless mode == 'none'
        puts(mode == 'unready' ? 'TEST unauthorized usb:1' :
          mode == 'network' ? 'TEST device product:ceres' : 'TEST device usb:1 product:ceres')
      end
      puts 'SECOND device usb:2 product:ceres' if mode == 'multiple'
      exit
    end
    abort 'unexpected target' unless ARGV.shift(3) == ['-s', 'TEST', 'shell']
    using_su = ARGV.first(2) == ['/system/xbin/su', '0']
    ARGV.shift(2) if using_su
    abort 'vendor su unavailable' if mode == 'su_denied' && using_su
    case ARGV
    when ['id', '-u'] then puts(mode == 'noroot' || (mode == 'su' && !using_su) ? '2000' : '0')
    when ['getprop', 'ro.product.model'] then puts 'YS-M33 fixture'
    when ['getprop', 'ro.build.version.release'] then puts '10'
    when ['getprop', 'ro.boot.flash.locked'] then puts '1'
    when ['getprop', 'ro.boot.verifiedbootstate'] then puts 'green'
    when ['cat', '/proc/device-tree/compatible']
      print(mode == 'foreign' ? "foreign,board\0" : "allwinner,a133\0")
    else
      if ARGV == ['readlink', '-f', '/dev/block/by-name/boot']
        puts(mode == 'target' ? '/dev/block/mmcblk1p3' : '/dev/block/mmcblk0p3')
      elsif ARGV == ['cat', '/sys/class/block/mmcblk0p3/start']
        puts '172032'
      elsif ARGV == ['cat', '/sys/class/block/mmcblk0p3/size']
        puts(mode == 'small' ? '32768' : '65536')
      else
        abort "forbidden command: #{ARGV.inspect}"
      end
    end
  FAKE
  File.chmod(0700, adb)
  %w[good su su_denied none multiple noroot foreign small target unready network timeout failure].each do |mode|
    extra = mode.start_with?('su') ? ['--su'] : []
    out, err, status = Open3.capture3({'PREFLIGHT_CARD' => mode},
      RbConfig.ruby, tool, '--adb', adb, '--timeout', '1', *extra)
    data = JSON.parse(out)
    if %w[good su].include?(mode)
      abort "good card rejected: #{err}" unless status.success? &&
        data['a133_candidate'] && data['root'] && data['boot']['size_sectors'] == 65536 &&
        data['flash_locked'] == '1' && data['writes_performed'] == 0 &&
        !data['installation_ready'] && !out.include?('TEST')
    else
      abort "unsafe #{mode} candidate accepted" if status.success? || data['installation_ready']
      abort 'preflight wrote anything' unless data['writes_performed'] == 0
    end
  end
end
puts 'Cable preflight: correct A133, missing/multiple devices, root, board and partition guards passed'
