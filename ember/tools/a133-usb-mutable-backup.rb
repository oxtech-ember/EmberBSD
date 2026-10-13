#!/usr/bin/env ruby
# Origin: EmberBSD - private read-only recovery UDISK/metadata copies with two matching reads.
require_relative 'a133-usb-backup'
require_relative 'a133-mutable-check'

module A133UsbBackup
  module_function
  def collect_mutable(directory:,serial:,cid:,adb:'adb',root_method:'adbd',timeout:3600)
    raise Invalid,'invalid_arguments' unless directory.is_a?(String) && timeout.is_a?(Integer) && (1..7200).cover?(timeout)
    raise Invalid,'invalid_mutable_identity' unless serial.is_a?(String) && serial.bytesize.between?(1,128) &&
      serial.match?(/\A[a-zA-Z0-9._-]+\z/) && cid.is_a?(String) && cid.match?(/\A[0-9a-f]{32}\z/)
    serial,cid=serial.dup.freeze,cid.dup.freeze
    directory=File.expand_path(directory); parent=File.dirname(directory)
    stat=File.lstat(parent)
    raise Invalid,'usb_backup_unsafe_parent' unless stat.directory? && stat.uid==Process.uid && stat.mode&07777==0700
    directory=File.join(File.realpath(parent),File.basename(directory))
    raise Invalid,'usb_backup_destination_exists' if File.exist?(directory) || File.symlink?(directory)
    source=Source.new(adb:adb,serial:serial,state:'recovery',root_method:root_method,timeout:timeout)
    Timeout.timeout(timeout,Deadline) do
      private_directory(File.dirname(directory)) do |parent_check|
        identity=source.mutable_inspect!
        raise Invalid,'usb_mutable_identity_mismatch' unless identity[:serial]==serial && identity[:cid]==cid
        begin
          Dir.mkdir(directory,0700)
        rescue Errno::EEXIST
          raise Invalid,'usb_backup_destination_exists'
        end
        private_directory(directory) do |directory_check|
          names={'UDISK'=>'udisk.raw','metadata'=>'metadata.raw'}
          results={}
          names.each do |role,name|
            results[role]=save_stream(directory,name) { |sink| source.read(role) { |chunk| sink.call(chunk) } }
          end
          names.each_key do |role|
            digest=Digest::SHA256.new
            bytes=source.read(role) { |chunk| digest.update(chunk) }
            raise Invalid,'usb_mutable_stream_changed' unless bytes==results.fetch(role)[:bytes] && digest.hexdigest==results.fetch(role)[:sha256]
          end
          record={'schema'=>1,'board'=>'ys-m33-a133','serial'=>serial,'cid'=>cid,'gpt_sha256'=>identity[:gpt_sha256],
            'root_method'=>identity[:root_method],'capture_state'=>'recovery','observation'=>'recovery_unmounted_two_matching_reads',
            'partitions'=>names.map { |role,name| {'role'=>role,'file'=>name,'bytes'=>results.fetch(role)[:bytes],
              'sha256'=>results.fetch(role)[:sha256]} }}
          manifest_text=JSON.generate(record); candidate=File.join(directory,'mutable.json.partial'); candidate_stat=nil
          File.open(candidate,File::WRONLY|File::CREAT|File::EXCL,0600) do |file|
            candidate_stat=file.stat
            raise Invalid,'usb_backup_host_file_error' unless file.write(manifest_text)==manifest_text.bytesize
            file.flush; file.fsync
          end
          retained_outputs(directory,names.values+['mutable.json.partial'],manifest_text,manifest_name:'mutable.json') do |check|
            verified=A133Mutable.verify(candidate,serial:serial,cid:cid,timeout:timeout)
            source.mutable_inspect!
            check.call(false); directory_check.call; parent_check.call
            publish(candidate,File.join(directory,'mutable.json'),candidate_stat)
            check.call(true); directory_check.call; parent_check.call
            {status:'usb_mutable_captured',host_files_created:3,saved_bytes:results.values.sum { |entry| entry[:bytes] },
              mutable_copies_verified:verified.fetch('mutable_copies_verified'),observation:verified.fetch('observation'),
              filesystem_consistency:verified.fetch('filesystem_consistency'),writes_performed:0,installation_ready:false}
          end
        end
      end
    end
  rescue Invalid,A133Mutable::Invalid,A133Usb::Invalid,A133Backup::Invalid => error
    raise Invalid,error.message,cause:nil
  rescue Deadline,Timeout::Error
    raise Invalid,'usb_backup_timeout',cause:nil
  rescue SystemCallError,IOError
    raise Invalid,'usb_backup_host_file_error',cause:nil
  rescue ArgumentError
    raise Invalid,'invalid_arguments',cause:nil
  end
end

if $PROGRAM_NAME==__FILE__
  result={writes_performed:0,installation_ready:false}
  begin
    options={adb:'adb',root_method:'adbd',timeout:3600}
    OptionParser.new do |parser|
      parser.on('--adb PATH') { |value| options[:adb]=value }
      parser.on('--serial ID') { |value| options[:serial]=value }
      parser.on('--cid HEX') { |value| options[:cid]=value }
      parser.on('--root-method METHOD') { |value| options[:root_method]=value }
      parser.on('--timeout SECONDS',Integer) { |value| options[:timeout]=value }
    end.parse!
    raise A133UsbBackup::Invalid,'invalid_arguments' unless ARGV.size==1 && options.key?(:serial) && options.key?(:cid)
    result.merge!(A133UsbBackup.collect_mutable(directory:ARGV.first,**options))
    puts JSON.pretty_generate(result)
  rescue A133UsbBackup::Invalid,OptionParser::ParseError => error
    result.merge!(status:'capture_stopped',reason:error.is_a?(A133UsbBackup::Invalid) ? error.message : 'invalid_arguments')
    puts JSON.pretty_generate(result)
    exit 1
  end
end
