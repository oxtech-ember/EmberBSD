#!/usr/bin/env ruby
# Origin: EmberBSD - fresh private UDISK/metadata evidence without consistency or restore claims.
require 'json'
require 'optparse'
require_relative 'a133-backup-check'

module A133Mutable
  class Invalid < StandardError; end
  class Object < Hash
    def []=(key,value)
      raise Invalid,'duplicate_mutable_key' if key?(key)
      super
    end
  end
  module_function

  def fields!(value,fields,reason)
    raise Invalid,reason unless value.is_a?(Hash) && value.keys.sort==fields.sort
  end

  def private_file(path,identities)
    before=File.lstat(path)
    safe=->(stat){stat.file? && stat.uid==Process.uid && stat.nlink==1 && stat.mode&07777==0600}
    raise Invalid,'unsafe_mutable_file' unless safe.call(before)
    File.open(path,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |file|
      opened=file.stat
      raise Invalid,'unsafe_mutable_file' unless safe.call(opened) && opened.dev==before.dev && opened.ino==before.ino
      key=[opened.dev,opened.ino]
      raise Invalid,'duplicate_mutable_inode' if identities.key?(key)
      identities[key]=true
      result=yield file,opened.size
      [file.stat,File.lstat(path)].each do |after|
        raise Invalid,'mutable_file_changed' unless safe.call(after) && [:dev,:ino,:size,:mtime,:ctime,:uid,:mode,:nlink].all? do |field|
          opened.public_send(field)==after.public_send(field)
        end
      end
      result
    end
  end

  def private_files(paths,identities,opened=[],&block)
    return yield opened if paths.empty?
    private_file(paths.first,identities) { |file,size| private_files(paths.drop(1),identities,opened+[[file,size]],&block) }
  end

  def record!(text,serial,cid,name)
    record=JSON.parse(text,object_class:Object,allow_duplicate_key:false)
    fields!(record,%w[schema board serial cid gpt_sha256 root_method capture_state observation partitions],'invalid_mutable_fields')
    raise Invalid,'unsupported_mutable_capture' unless record['schema'].is_a?(Integer) && record['schema']==1 && record['board']=='ys-m33-a133'
    raise Invalid,'mutable_identity_mismatch' unless record['serial']==serial && record['cid']==cid
    raise Invalid,'invalid_mutable_source' unless record['capture_state']=='recovery' &&
      %w[adbd vendor_su].include?(record['root_method']) && record['observation']=='recovery_unmounted_two_matching_reads'
    raise Invalid,'invalid_mutable_sha256' unless record['gpt_sha256'].is_a?(String) && record['gpt_sha256'].match?(/\A[0-9a-f]{64}\z/)
    parts=record['partitions']
    raise Invalid,'invalid_mutable_partitions' unless parts.is_a?(Array) && parts.size==2
    parts.each { |part| fields!(part,%w[role file bytes sha256],'invalid_mutable_partitions') }
    raise Invalid,'invalid_mutable_partitions' unless parts.map { |part| part['role'] }.sort==%w[UDISK metadata] && parts.all? do |part|
      profile=A133Backup::INVENTORY.find { |entry| entry[:name]==part['role'] }
      profile && part['bytes'].is_a?(Integer) && part['bytes']==profile[:sectors]*512 &&
        part['sha256'].is_a?(String) && part['sha256'].match?(/\A[0-9a-f]{64}\z/)
    end
    names=parts.map { |part| part['file'] }
    raise Invalid,'invalid_mutable_filename' unless names.all? do |value|
      value.is_a?(String) && value.bytesize.between?(1,128) && value.match?(/\A[a-zA-Z0-9][a-zA-Z0-9._-]*\z/)
    end
    raise Invalid,'duplicate_mutable_filename' unless names.uniq.size==2 && !names.include?(name)
    record
  end

  def verify(path,serial:,cid:,timeout:3600)
    raise Invalid,'invalid_mutable_identity' unless serial.is_a?(String) && serial.bytesize.between?(1,128) &&
      serial.match?(/\A[a-zA-Z0-9._-]+\z/) && cid.is_a?(String) && cid.match?(/\A[0-9a-f]{32}\z/)
    raise Invalid,'invalid_arguments' unless path.is_a?(String) && timeout.is_a?(Integer) && (1..7200).cover?(timeout)
    serial,cid=serial.dup.freeze,cid.dup.freeze
    path=File.expand_path(path); directory=File.dirname(path)
    before=File.lstat(directory)
    raise Invalid,'unsafe_mutable_directory' unless before.directory? && before.uid==Process.uid && before.mode&07777==0700
    directory=File.realpath(directory); path=File.join(directory,File.basename(path))
    Timeout.timeout(timeout) do
      File.open(directory,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |dir|
        directory_check=lambda do
          [dir.stat,File.lstat(directory)].each do |stat|
            raise Invalid,'unsafe_mutable_directory' unless stat.directory? && stat.uid==Process.uid &&
              stat.mode&07777==0700 && stat.dev==before.dev && stat.ino==before.ino
          end
        end
        directory_check.call
        identities={}
        result=private_file(path,identities) do |manifest,size|
          raise Invalid,'mutable_manifest_too_large' if size>65536
          text=manifest.read(65537) || ''.b
          raise Invalid,'mutable_manifest_too_large' if text.bytesize>65536
          record=record!(text,serial,cid,File.basename(path))
          private_files(record['partitions'].map { |part| File.join(directory,part['file']) },identities) do |opened|
            record['partitions'].each_with_index do |part,index|
              file,size=opened.fetch(index)
              raise Invalid,'mutable_copy_size_mismatch' unless size==part['bytes']
              digest=Digest::SHA256.new
              while (chunk=file.read(1048576))
                digest.update(chunk)
              end
              raise Invalid,'mutable_copy_hash_mismatch' unless digest.hexdigest==part['sha256']
            end
            {'status'=>'mutable_integrity_verified','serial'=>serial,'cid'=>cid,'gpt_sha256'=>record['gpt_sha256'],
              'partition_sha256'=>record['partitions'].to_h { |part| [part['role'],part['sha256']] },
              'mutable_copies_verified'=>true,'observation'=>record['observation'],
              'filesystem_consistency'=>'not_established_by_integrity_check','writes_performed'=>0,'installation_ready'=>false}
          end
        end
        directory_check.call
        result
      end
    end
  rescue Invalid => error
    raise Invalid,error.message,cause:nil
  rescue JSON::ParserError => error
    raise Invalid,error.message.start_with?('duplicate key ') ? 'duplicate_mutable_key' : 'invalid_mutable_json',cause:nil
  rescue Timeout::Error
    raise Invalid,'mutable_read_timeout',cause:nil
  rescue EncodingError,ArgumentError,TypeError
    raise Invalid,'invalid_mutable_json',cause:nil
  rescue SystemCallError,IOError
    raise Invalid,'mutable_file_unavailable',cause:nil
  end
  private_class_method :fields!,:private_file,:private_files,:record!
end

if $PROGRAM_NAME==__FILE__
  result={writes_performed:0,installation_ready:false}
  begin
    options={timeout:3600}
    OptionParser.new do |parser|
      parser.on('--serial ID') { |value| options[:serial]=value }
      parser.on('--cid HEX') { |value| options[:cid]=value }
      parser.on('--timeout SECONDS',Integer) { |value| options[:timeout]=value }
    end.parse!
    raise A133Mutable::Invalid,'invalid_arguments' unless ARGV.size==1 && options.key?(:serial) && options.key?(:cid)
    receipt=A133Mutable.verify(ARGV.first,**options)
    result.merge!(status:receipt['status'],mutable_copies_verified:true,observation:receipt['observation'],
      filesystem_consistency:receipt['filesystem_consistency'])
    puts JSON.pretty_generate(result)
  rescue A133Mutable::Invalid,OptionParser::ParseError => error
    result.merge!(status:'inspection_stopped',reason:error.is_a?(A133Mutable::Invalid) ? error.message : 'invalid_arguments')
    puts JSON.pretty_generate(result)
    exit 1
  end
end
