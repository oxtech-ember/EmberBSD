#!/usr/bin/env ruby
# Origin: EmberBSD - read-only Android USB inspection before A133 installation.
# No root restart, unlock, reboot, transfer, storage write or shell script.
require 'json'
require 'open3'
require 'optparse'
require 'timeout'

class PreflightError < StandardError; end

def read_adb(adb, timeout, *arguments)
  output = nil
  Open3.popen3(adb, *arguments, pgroup: true) do |input, out, err, waiter|
    input.close
    begin
      Timeout.timeout(timeout) do
        output = out.read
        err.read
        raise PreflightError, 'adb_read_failed' unless waiter.value.success?
      end
    rescue Timeout::Error
      begin
        Process.kill('KILL', -waiter.pid)
      rescue Errno::ESRCH
        # The process already exited while the timeout was being delivered.
      end
      raise PreflightError, 'adb_read_timeout'
    end
  end
  output
end

result = {writes_performed: 0, installation_ready: false}
begin
  options = {adb: 'adb', timeout: 8, su: false}
  OptionParser.new do |parser|
    parser.banner = 'usage: a133-install-preflight.rb [--adb PATH] [--serial ID] [--timeout SECONDS] [--su]'
    parser.on('--adb PATH') { |value| options[:adb] = value }
    parser.on('--serial ID') { |value| options[:serial] = value }
    parser.on('--timeout SECONDS', Integer) { |value| options[:timeout] = value }
    parser.on('--su') { options[:su] = true }
  end.parse!
  raise PreflightError, 'invalid_arguments' unless ARGV.empty? && (1..30).cover?(options[:timeout])
  inventory = read_adb(options[:adb], options[:timeout], 'devices', '-l')
  devices = inventory.lines.map do |line|
    fields = line.split
    next if fields.empty? || fields.first == 'List' || fields.first.start_with?('*')
    {serial: fields[0], state: fields[1], usb: fields.any? { |field| field.start_with?('usb:') }}
  end.compact
  candidates = options[:serial] ? devices.select { |device| device[:serial] == options[:serial] } : devices
  result[:device_count] = candidates.size
  raise PreflightError, 'expected_one_device' unless candidates.size == 1
  device = candidates.first
  result[:usb] = device[:usb]
  result[:adb_state] = device[:state]
  raise PreflightError, 'usb_device_not_ready' unless device[:usb] && %w[device recovery].include?(device[:state])
  prefix = options[:su] ? ['/system/xbin/su', '0'] : []
  query = ->(*args) { read_adb(options[:adb], options[:timeout], '-s', device[:serial], 'shell', *prefix, *args).strip }
  result[:root_method] = options[:su] ? 'vendor_su' : 'adbd'
  result[:root] = query.call('id', '-u') == '0'
  raise PreflightError, 'root_read_access_required' unless result[:root]
  result[:model] = query.call('getprop', 'ro.product.model')
  result[:android_release] = query.call('getprop', 'ro.build.version.release')
  result[:flash_locked] = query.call('getprop', 'ro.boot.flash.locked')
  result[:verified_boot_state] = query.call('getprop', 'ro.boot.verifiedbootstate')
  compatible = query.call('cat', '/proc/device-tree/compatible').split("\0")
  result[:a133_candidate] = compatible.include?('allwinner,a133')
  raise PreflightError, 'unexpected_board' unless result[:a133_candidate]
  target = query.call('readlink', '-f', '/dev/block/by-name/boot')
  raise PreflightError, 'unexpected_boot_target' unless target == '/dev/block/mmcblk0p3'
  start = query.call('cat', '/sys/class/block/mmcblk0p3/start')
  size = query.call('cat', '/sys/class/block/mmcblk0p3/size')
  raise PreflightError, 'invalid_partition_numbers' unless start.match?(/\A[0-9]+\z/) && size.match?(/\A[0-9]+\z/)
  result[:boot] = {start_sector: start.to_i, size_sectors: size.to_i}
  raise PreflightError, 'unexpected_boot_bounds' unless start.to_i == 172032 && size.to_i == 65536
  result[:status] = 'inspection_complete'
  result[:remaining] = ['exact_board_identification', 'full_partition_inventory',
    'per_device_backup', 'verified_cable_unlock_and_recovery', 'image_validation']
  puts JSON.pretty_generate(result)
rescue PreflightError, OptionParser::ParseError, Errno::ENOENT => error
  result[:status] = 'inspection_stopped'
  result[:reason] = error.is_a?(PreflightError) ? error.message : 'invalid_host_configuration'
  puts JSON.pretty_generate(result)
  exit 1
end
