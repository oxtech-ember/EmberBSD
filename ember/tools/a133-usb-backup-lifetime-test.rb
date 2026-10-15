#!/usr/bin/env ruby
# Origin: EmberBSD - review regressions for acquisition evidence through publication.
require 'tmpdir'
require_relative 'a133-usb-backup'
require_relative 'a133-capture-test-support'
CaptureFixture.configure
module BackupLateSourceMutation
  def inspect!
    identity = super
    fault = $backup_lifetime_fault
    if fault && File.exist?(File.join(fault[:directory],'capture.json.partial'))
      $backup_lifetime_fault = nil
      case fault[:kind]
      when :boot
        File.binwrite(File.join(fault[:directory],'boot0.bin'),'X'*1024)
      when :manifest
        File.binwrite(File.join(fault[:directory],'capture.json.partial'),'INVALID_CAPTURE_PRIVATE_MARKER')
      when :directory
        File.chmod(0755,fault[:directory])
      else
        $backup_publish_fault = fault
      end
    end
    identity
  end
end
A133UsbBackup::Source.prepend(BackupLateSourceMutation)
module BackupLatePublicationMutation
  def fsync
    result = super
    fault = $backup_publish_fault
    if fault && File.identical?(path,fault[:directory]) && File.exist?(File.join(path,'capture.json'))
      $backup_publish_fault = nil
      if fault[:kind]==:post_boot
        File.binwrite(File.join(path,'boot0.bin'),'X'*1024)
      else
        manifest = File.join(path,'capture.json')
        time = File.stat(manifest).mtime
        text = File.binread(manifest).sub('0123456789abcdef0123456789abcdef','ffffffffffffffffffffffffffffffff')
        File.binwrite(manifest,text)
        File.utime(time,time,manifest)
      end
    end
    result
  end
end
File.prepend(BackupLatePublicationMutation)
checks = 0
failures = []
Dir.mktmpdir('backup-lifetime-') do |parent|
  File.chmod(0700,parent)
  source = File.join(parent,'source'); Dir.mkdir(source,0700)
  CaptureFixture.write(source)
  File.binwrite(File.join(source,'boot0.bin'),'e'*1024)
  File.binwrite(File.join(source,'boot1.bin'),'f'*1024)
  adb = File.join(source,'adb')
  File.write(adb,File.read(File.join(__dir__,'a133-usb-backup-fixture.rb')))
  File.chmod(0700,adb)
  File.write(File.join(source,'state.json'),'{}')
  [:boot,:manifest,:directory,:post_boot,:post_manifest].each do |kind|
    directory = File.join(parent,kind.to_s)
    $backup_lifetime_fault = {directory:directory,kind:kind}
    begin
      A133UsbBackup.collect(directory:directory,adb:adb,timeout:30)
      failures << "#{kind}: changed evidence accepted"
    rescue A133UsbBackup::Invalid => error
      if %w[usb_backup_output_changed usb_backup_unsafe_directory].include?(error.message) &&
        error.cause.nil? && !error.full_message.include?('INVALID_CAPTURE_PRIVATE_MARKER')
        checks += 1
      else
        failures << "#{kind}: unexpected #{error.message}"
      end
      failures << "#{kind}: prepublication failure left final manifest" if
        [:boot,:manifest,:directory].include?(kind) && File.exist?(File.join(directory,'capture.json'))
    ensure
      failures << "#{kind}: fault did not run" if $backup_lifetime_fault || $backup_publish_fault
      File.chmod(0700,directory) if File.directory?(directory)
      $backup_lifetime_fault = $backup_publish_fault = nil
    end
  end
end
abort failures.map { |name| 'FAIL: '+name }.join("\n") unless failures.empty?
puts "USB backup lifetime: #{checks} late evidence/publication cases passed"
