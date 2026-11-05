#!/usr/bin/env ruby
# Origin: EmberBSD - a new boot between SSH inspection and the mutating shell.
require 'tmpdir'
require 'rbconfig'
require 'base64'
require 'shellwords'
require_relative 'a133-native-source'
Dir.mktmpdir('a133-native-epoch-') do |dir|
  key=Base64.strict_encode64([11].pack('N')+'ssh-ed25519'+[32].pack('N')+'k'*32)
  known=File.join(dir,'known');identity=File.join(dir,'identity')
  File.write(known,"test-alias ssh-ed25519 #{key}\n");File.write(identity,'TEST_ONLY')
  [known,identity].each { |path| File.chmod(0600,path) }
  marker=File.join(dir,'marker');env=File.join(dir,'env')
  bin=File.join(dir,'bin');Dir.mkdir(bin,0700)
  File.write(File.join(bin,'sysctl'),"#!/bin/sh\nprintf '%s\\n' 'a different boot'\n")
  File.write(File.join(bin,'uname'),"#!/bin/sh\nprintf '%s\\n' 'fixture kernel'\n")
  File.write(File.join(bin,'stat'),"#!/bin/sh\nprintf '%s\\n' '0:600:49:1'\n")
  File.write(File.join(bin,'sync'),"#!/bin/sh\nexit 0\n")
  %w[sysctl uname stat sync].each { |name| File.chmod(0700,File.join(bin,name)) }
  ssh=File.join(dir,'ssh')
  File.write(ssh,<<~SCRIPT)
    #!#{RbConfig.ruby}
    require 'shellwords'
    prefix='PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH; set -e; '
    command=ARGV.last
    abort 'unexpected SSH shell prefix' unless command.start_with?(prefix)
    command='set -e; '+command.delete_prefix(prefix)
    command=command.gsub('/etc/ember/firstboot-pending',Shellwords.escape(#{marker.inspect}))
    command=command.gsub('/dev/rdk1',Shellwords.escape(#{env.inspect}))
    exec({'PATH'=>#{(bin+':/usr/bin:/bin').inspect}},'/bin/sh','-c',command)
  SCRIPT
  File.chmod(0700,ssh)
  client=A133Native::Source.new(ssh:ssh,host:'192.0.2.1',host_alias:'test-alias',
    known_hosts:known,identity:identity,host_key:'ssh-ed25519 '+key,kernel_version:'fixture kernel',timeout:5)
  # The separate inspection saw the old boot. The next remote shell runs after
  # the injected reset. Its own sysctl reports the new boot.
  def client.epoch;'fixture epoch';end
  body="fixture=epoch\0\0".b.ljust(131068,"\xff".b)
  prefix=[Zlib.crc32(body)].pack('V')+body
  original='unchanged'.b.ljust(131072,"\xa5".b)+'TAIL'
  nonce='4'*48
  operations={
    write:-> { client.write_environment(prefix:prefix,epoch:'fixture epoch') },
    confirm:-> { client.confirm(nonce:nonce,epoch:'fixture epoch') }
  }
  operations.each do |name,operation|
    File.binwrite(env,original);File.write(marker,nonce+"\n")
    begin
      operation.call;abort 'FAIL: mutation accepted a different boot'
    rescue A133Cable::Invalid=>error
      abort 'FAIL: wrong epoch refusal '+error.message unless %w[native_command_failed native_pipe_failed].include?(error.message) && error.cause.nil?
    end
    abort 'FAIL: mutation occurred before epoch refusal' unless File.binread(env)==original && File.read(marker)==nonce+"\n"
    # Causal negative control: the old separate-check behavior reaches the
    # mutation on exactly the same fixture inputs.
    original_guard=client.method(:epoch_guard)
    client.define_singleton_method(:epoch_guard) { |_value| '' }
    operation.call
    changed=name==:write ? File.binread(env)!=original : !File.exist?(marker)
    abort 'FAIL: stale precheck negative control did not reach mutation' unless changed
    client.define_singleton_method(:epoch_guard,original_guard)
    # A different RAM kernel can coexist with the expected installed boot
    # partition. The running version must also match inside the mutating shell.
    File.binwrite(env,original);File.write(marker,nonce+"\n")
    File.write(File.join(bin,'sysctl'),"#!/bin/sh\nprintf '%s\\n' 'fixture epoch'\n")
    File.write(File.join(bin,'uname'),"#!/bin/sh\nprintf '%s\\n' 'different kernel'\n")
    begin
      operation.call;abort 'FAIL: mutation accepted a different running kernel'
    rescue A133Cable::Invalid=>error
      abort 'FAIL: wrong kernel refusal' unless %w[native_command_failed native_pipe_failed].include?(error.message)
    end
    abort 'FAIL: wrong kernel changed data' unless File.binread(env)==original && File.read(marker)==nonce+"\n"
    original_kernel_guard=client.method(:kernel_guard)
    client.define_singleton_method(:kernel_guard) { '' }
    operation.call
    changed=name==:write ? File.binread(env)!=original : !File.exist?(marker)
    abort 'FAIL: missing version guard control did not reach mutation' unless changed
    client.define_singleton_method(:kernel_guard,original_kernel_guard)
    File.write(File.join(bin,'sysctl'),"#!/bin/sh\nprintf '%s\\n' 'a different boot'\n")
    File.write(File.join(bin,'uname'),"#!/bin/sh\nprintf '%s\\n' 'fixture kernel'\n")
  end
end
puts 'Native epoch/kernel: both mutating shells reject changed identity; missing-guard controls reach the mutation'
