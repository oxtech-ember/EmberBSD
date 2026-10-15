#!/usr/bin/env ruby
# Origin: EmberBSD - host-only artifact manifest check before cable installation.
require 'digest'
require 'json'

class BundleError < StandardError; end

class BundleObject < Hash
  def []=(key, value)
    raise BundleError, 'duplicate_json_key' if key?(key)
    super
  end
end

module A133Bundle
  module_function
  def bundle_file(path)
    raise BundleError, 'bundle_file_not_regular' unless File.lstat(path).file?
    File.open(path, File::RDONLY | File::NOFOLLOW | File::NONBLOCK) do |file|
      before = file.stat
      raise BundleError, 'bundle_file_not_regular' unless before.file?
      result = yield file, before.size
      after = file.stat
      fields = [:dev, :ino, :size, :mtime, :ctime]
      raise BundleError, 'bundle_file_changed' unless
        fields.all? { |field| before.public_send(field) == after.public_send(field) }
      result
    end
  end

  def exact_fields(object, fields, reason)
    raise BundleError, reason unless object.is_a?(Hash) && object.keys.sort == fields.sort
  end

  def verify(path)
    result = {writes_performed: 0, installation_ready: false}
    begin
      raise BundleError, 'invalid_arguments' unless path.is_a?(String)
      path = File.expand_path(path)
      text = bundle_file(path) do |file, size|
        raise BundleError, 'manifest_too_large' if size > 65536
        bytes = file.read(65537) || ''.b
        raise BundleError, 'manifest_too_large' if bytes.bytesize > 65536
        bytes
      end
      # Recent JSON parsers collapse pairs before object_class assignment;
      # their native guard and the older parser's Hash guard are both needed.
      manifest = JSON.parse(text, object_class: BundleObject, allow_duplicate_key: false)
      exact_fields(manifest, %w[schema board source_commit artifacts], 'invalid_manifest_fields')
      raise BundleError, 'unsupported_manifest' unless manifest['schema'] == 1 &&
        manifest['schema'].is_a?(Integer) && manifest['board'] == 'ys-m33-a133'
      raise BundleError, 'invalid_source_commit' unless manifest['source_commit'].is_a?(String) &&
        manifest['source_commit'].match?(/\A[0-9a-f]{40}\z/)
      artifacts = manifest['artifacts']
      raise BundleError, 'invalid_artifacts' unless artifacts.is_a?(Array) && artifacts.size == 3
      artifacts.each do |artifact|
        exact_fields(artifact, %w[role file bytes sha256], 'invalid_artifact_fields')
      end
      roles = artifacts.map { |artifact| artifact['role'] }
      raise BundleError, 'invalid_roles' unless roles.all? { |role| role.is_a?(String) } &&
        roles.sort == %w[boot resources root]
      files = artifacts.map { |artifact| artifact['file'] }
      raise BundleError, 'invalid_filename' unless files.all? do |name|
        name.is_a?(String) && name.bytesize.between?(1, 128) &&
          name.match?(/\A[a-zA-Z0-9][a-zA-Z0-9._-]*\z/)
      end
      raise BundleError, 'duplicate_filename' unless files.uniq.size == 3
      bounds = {
        'boot' => [172032, 33554432], 'resources' => [73728, 33554432],
        'root' => [6565888, 27676098048]
      }
      artifacts.each do |artifact|
        size, role, sha = artifact.values_at('bytes', 'role', 'sha256')
        raise BundleError, 'invalid_sha256' unless sha.is_a?(String) && sha.match?(/\A[0-9a-f]{64}\z/)
        raise BundleError, 'invalid_image_size' unless size.is_a?(Integer) && size > 0 &&
          size % 512 == 0 && size <= bounds.fetch(role).last &&
          (role == 'root' || size == 33554432)
      end
      directory = File.realpath(File.dirname(path))
      verified = artifacts.map do |artifact|
        role, size, sha = artifact.values_at('role', 'bytes', 'sha256')
        digest = bundle_file(File.join(directory, artifact['file'])) do |file, actual_size|
          raise BundleError, 'image_size_mismatch' unless actual_size == size
          hash = Digest::SHA256.new
          remaining = size
          while remaining > 0
            chunk = file.read([remaining, 1048576].min)
            raise BundleError, 'bundle_file_changed' if chunk.nil? || chunk.empty?
            hash.update(chunk)
            remaining -= chunk.bytesize
          end
          raise BundleError, 'bundle_file_changed' unless file.read(1).nil?
          hash.hexdigest
        end
        raise BundleError, 'image_sha256_mismatch' unless digest == sha
        {role: role, bytes: size, sha256: digest, start_sector: bounds.fetch(role).first}
      end
      result[:status] = 'artifact_manifest_verified'
      result[:source_commit] = manifest['source_commit']
      result[:artifacts] = verified
      {receipt: result, paths: artifacts.each_with_object({}) { |artifact, map| map[artifact['role']] = File.join(directory, artifact['file']) }}
    rescue BundleError, JSON::ParserError, EncodingError, ArgumentError, SystemCallError, IOError => error
      reason = if error.is_a?(BundleError)
        error.message
      elsif error.is_a?(JSON::ParserError) && error.message.start_with?('duplicate key ')
        'duplicate_json_key'
      elsif error.is_a?(JSON::ParserError) || error.is_a?(EncodingError) || error.is_a?(ArgumentError)
        'invalid_json'
      else
        'bundle_file_unavailable'
      end
      raise BundleError, reason
    end
  end
end

if $PROGRAM_NAME == __FILE__
  result = {writes_performed: 0, installation_ready: false}
  begin
    raise BundleError, 'invalid_arguments' unless ARGV.size == 1
    puts JSON.pretty_generate(A133Bundle.verify(ARGV.first)[:receipt])
  rescue BundleError => error
    result[:status] = 'inspection_stopped'
    result[:reason] = error.message
    puts JSON.pretty_generate(result)
    exit 1
  end
end
