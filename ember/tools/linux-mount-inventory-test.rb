#!/usr/bin/env ruby
# Origin: EmberBSD - bounded streaming and live-task inventory contracts.
require 'tmpdir'
require 'open3'
require 'shellwords'
require 'rbconfig'
tool=File.join(__dir__,'linux-mount-inventory.rb')
abort 'FAIL: streaming mount inventory missing' unless File.file?(tool)
require tool
root="1 0 0:1 / / rw - rootfs rootfs rw\n"
target="2 1 179:2 / /hidden rw - ext4 alias rw\n"
checks=0
reader=LinuxMountInventory::Reader.new
large=root*100000+target.chomp
offset=0
while offset<large.bytesize
  reader.feed(large.byteslice(offset,65521));offset+=65521
end
abort 'FAIL: large inventory lost late device' unless reader.finish==%w[0:1 179:2]
checks+=1
reject=lambda do |reason,&body|
  begin
    body.call; abort 'FAIL: unsafe inventory accepted'
  rescue LinuxMountInventory::Invalid => error
    abort "FAIL: expected #{reason}, got #{error.message}" unless error.message==reason
  end
  checks+=1
end
reject.call('mount_inventory_invalid') {LinuxMountInventory::Reader.new.finish}
reject.call('mount_inventory_invalid') do
  r=LinuxMountInventory::Reader.new;r.feed(root*40000);r.feed('broken');r.finish
end
reject.call('mount_inventory_invalid') do
  r=LinuxMountInventory::Reader.new;r.feed("1 0 private / / rw - rootfs rootfs rw\n");r.finish
end
reject.call('mount_inventory_too_large') do
  r=LinuxMountInventory::Reader.new;r.feed('x'*65537)
end
reject.call('mount_inventory_too_large') do
  r=LinuxMountInventory::Reader.new;r.feed("1 0 0:1 / / #{'x'*65536} - rootfs rootfs rw\n")
end
reject.call('mount_inventory_too_large') do
  r=LinuxMountInventory::Reader.new
  4097.times { |i| r.feed("1 0 1:#{i} / / rw - rootfs rootfs rw\n") }
end
reject.call('mount_inventory_too_large') do
  r=LinuxMountInventory::Reader.new;chunk=root*20000
  120.times {r.feed(chunk)}
end
Dir.mktmpdir('linux-mounts-') do |dir|
  proc=File.join(dir,'proc tree');Dir.mkdir(proc)
  make=lambda do |pid,tid,line|
    p=File.join(proc,pid.to_s);Dir.mkdir(p) unless File.directory?(p)
    File.write(File.join(p,'stat'),'live')
    tasks=File.join(p,'task');Dir.mkdir(tasks) unless File.directory?(tasks)
    t=File.join(tasks,tid.to_s);Dir.mkdir(t)
    File.write(File.join(t,'stat'),'live');File.write(File.join(t,'mountinfo'),line)
    t
  end
  make.call(101,101,root);hidden=make.call(101,102,target)
  gone=make.call(202,202,root)
  bin=File.join(dir,'bin');Dir.mkdir(bin)
  cat=File.join(bin,'cat')
  File.write(cat,<<~RUBY)
    #!#{RbConfig.ruby}
    if File.exist?('gone')
      File.unlink('stat');exit 1
    end
    exit 1 if File.exist?('unreadable')
    exec '/bin/cat',*ARGV
  RUBY
  File.chmod(0700,cat)
  File.write(File.join(gone,'gone'),'1')
  command=LinuxMountInventory::COMMAND.gsub('/proc/',Shellwords.escape(proc)+'/')
  out,err,status=Open3.capture3({'PATH'=>bin+':'+ENV.fetch('PATH')},'/bin/sh','-c',command)
  r=LinuxMountInventory::Reader.new;r.feed(out)
  abort 'FAIL: task traversal missed hidden mount or rejected departed task' unless status.success? && err.empty? && r.finish==%w[0:1 179:2]
  checks+=1
  File.write(File.join(hidden,'unreadable'),'1')
  _,_,status=Open3.capture3({'PATH'=>bin+':'+ENV.fetch('PATH')},'/bin/sh','-c',command)
  abort 'FAIL: unreadable live task ignored' if status.success?
  checks+=1
end
puts "Linux mount inventory: #{checks} stream and actual-shell cases passed"
