#!/usr/bin/env ruby
# Origin: EmberBSD - one native reboot boundary with independent vendor-save simulation.
require 'json'
base=File.dirname(__FILE__)
state_path=File.join(base,'state.json')
state=JSON.parse(File.read(state_path))
serial='PROTECTION_TEST_SERIAL'
if ARGV==['devices','-l']
  File.write(File.join(base,'observed'),'1')
  if state['reboot_started']
    sleep 10 if state['inventory_stall']
    sequence=state.fetch('pending_states',[])
    row_state=sequence.empty? ? state.fetch('destination') : sequence.shift
    state['pending_states']=sequence
    state['state']=row_state if %w[device recovery].include?(row_state)
    File.write(state_path,JSON.generate(state))
    File.open(File.join(base,'polls'),'a') { |f| f.puts row_state }
  else
    row_state=state.fetch('state','device')
  end
  puts 'List of devices attached'
  puts 'UNRELATED_DEVICE device usb:other'
  unless row_state=='missing'
    suffix=state['transport']=='tcp' ? 'product:fixture' : 'usb:fixture'
    puts "#{serial} #{row_state} #{suffix}"
    puts "#{serial} #{row_state} #{suffix}" if state['duplicate']
  end
  exit
end
if ARGV==['-s',serial,'reboot']
  File.open(File.join(base,'rebooted'),'a') { |f| f.puts state.fetch('state') }
  source=state.fetch('state')
  if source=='device'
    File.open(File.join(base,'part2'),'r+b') { |f| f.write(File.binread(File.join(base,'consumed-prefix.bin'))) }
  end
  state['reboot_started']=true
  state['destination']=source=='device' ? 'recovery' : 'device'
  state['pending_states']=state.fetch('sequence',[])
  state.merge!(state.fetch('late',{}))
  if state['corrupt_role']
    index={'bootloader'=>1,'env'=>2,'boot'=>3,'recovery'=>6}.fetch(state['corrupt_role'])
    File.open(File.join(base,"part#{index}"),'r+b') { |f| f.write('X') }
  end
  if state['tail_corrupt']
    File.open(File.join(base,'part2'),'r+b') { |f| f.seek(16777215); f.write('X') }
  end
  if state['gpt_changed']
    require 'zlib'
    File.open(File.join(base,'disk'),'r+b') do |f|
      [1,60620799].each do |lba|
        f.seek(lba*512); header=f.read(512); header[56,16]='G'*16; header[16,4]=[0].pack('V')
        header[16,4]=[Zlib.crc32(header.byteslice(0,92))].pack('V'); f.seek(lba*512); f.write(header)
      end
    end
  end
  File.write(state_path,JSON.generate(state))
  File.write(File.join(base,'reboot-clock'),Process.clock_gettime(Process::CLOCK_MONOTONIC).to_s)
  case state['reboot_mode']
  when 'failure' then STDERR.write('PRIVATE_REBOOT_DIAGNOSTIC'); exit 2
  when 'noisy' then STDERR.write('PRIVATE_REBOOT_DIAGNOSTIC')
  when 'stall' then sleep 10
  when 'launcher_invalid'
    File.rename(File.join(base,'launch'),File.join(base,'launch-retained'))
    File.write(File.join(base,'launch'),'PRIVATE_HOST_LOCATION')
  end
  exit
end
load File.join(base,'adb-source.rb')
