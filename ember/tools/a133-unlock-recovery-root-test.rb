#!/usr/bin/env ruby
# Origin: EmberBSD - bounded recovery root readiness without root or reboot commands.
require 'tmpdir'
require 'rbconfig'
require_relative 'a133-unlock-stage'

checks=0
Dir.mktmpdir('unlock-root-') do |dir|
  adb=File.join(dir,'adb')
  state=File.join(dir,'state.json')
  calls=File.join(dir,'calls')
  File.write(adb,<<~RUBY)
    \#!#{RbConfig.ruby}
    require 'json'
    File.open(#{calls.inspect},'a') { |f| f.puts(JSON.generate(ARGV)) }
    if ARGV==['devices','-l']
      puts 'TEST_SERIAL recovery usb:fixture'; exit
    end
    abort 'unexpected command' unless ARGV==['-s','TEST_SERIAL','shell','-T','id -u']
    values=JSON.parse(File.read(#{state.inspect}))
    value=values.size>1 ? values.shift : values.first
    File.write(#{state.inspect},JSON.generate(values))
    exit 1 if value=='disconnect'
    puts value
  RUBY
  File.chmod(0700,adb)
  run=lambda do |values,root='adbd'|
    File.write(state,JSON.generate(values));File.write(calls,'')
    stage=A133Unlock::Stage.allocate
    {adb:adb,serial:'TEST_SERIAL',timeout:1,wait_timeout:2,roots:{'recovery'=>root}}.each do |key,value|
      stage.instance_variable_set('@'+key.to_s,value)
    end
    stage.send(:wait_recovery)
  end
  run.call(%w[2000 disconnect 0])
  observed=File.readlines(calls).map { |line| JSON.parse(line) }
  abort 'FAIL: returned before caller worker established root' unless observed.count { |args| args.last=='id -u' }==3
  checks+=1
  %w[1000 malformed].each do |uid|
    begin
      run.call([uid]);abort 'FAIL: unexpected recovery identity accepted'
    rescue A133Unlock::Invalid => error
      abort 'FAIL: unexpected identity reason' unless error.message=='usb_root_required'
    end
    checks+=1
  end
  begin
    run.call(['2000']);abort 'FAIL: non-root recovery accepted'
  rescue A133Unlock::Invalid => error
    abort 'FAIL: root wait did not expire' unless error.message=='usb_unlock_wait_timeout'
  end
  checks+=1
  run.call(['2000'],'vendor_su')
  abort 'FAIL: vendor su wait probed raw adbd UID' unless File.readlines(calls).size==1
  checks+=1
end
puts "Unlock recovery root: #{checks} transition, refusal and timeout cases passed"
