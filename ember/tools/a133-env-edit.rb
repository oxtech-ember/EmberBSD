#!/usr/bin/env ruby
# Origin: EmberBSD - edit a copied CRC32 vendor environment, never a device.
require 'zlib'

module A133Env
  SIZE = 131072
  class Invalid < StandardError; end
  module_function

  def decode(data)
    raise Invalid, 'environment must be exactly 128 KiB' unless data.bytesize == SIZE
    body = data.byteslice(4, SIZE - 4)
    raise Invalid, 'environment CRC32 mismatch' unless Zlib.crc32(body) == data.byteslice(0, 4).unpack1('V')
    leading_empty = body.start_with?("\0")
    text = leading_empty ? body.byteslice(1..) : body
    stop = text.index("\0\0")
    raise Invalid, 'environment terminator missing' unless stop
    entries = text.byteslice(0, stop).split("\0").map do |line|
      name, value = line.split('=', 2)
      raise Invalid, 'malformed variable' unless name.match?(/\A[A-Za-z_][A-Za-z0-9_]*\z/) && value
      [name, value]
    end
    raise Invalid, 'duplicate variable names' unless entries.map(&:first).uniq.size == entries.size
    padding = text.byteslice((stop + 2)..).bytes.uniq
    raise Invalid, 'unsupported padding' unless padding.empty? || padding == [0] || padding == [255]
    {entries: entries, leading_empty: leading_empty, pad_byte: padding.first || 0}
  end

  def patch(data, updates)
    parsed = decode(data)
    updates.each do |name, value|
      raise Invalid, 'invalid update' unless name.is_a?(String) && value.is_a?(String) &&
        name.match?(/\A[A-Za-z_][A-Za-z0-9_]*\z/) && !value.include?("\0")
    end
    seen = []
    entries = parsed[:entries].map do |name, value|
      seen << name
      value = updates[name] if updates.key?(name)
      next if updates.key?(name) && value.empty?
      [name, value]
    end.compact
    updates.each { |name, value| entries << [name, value] unless seen.include?(name) || value.empty? }
    body = parsed[:leading_empty] ? "\0".b : ''.b
    body << entries.map { |name, value| name.b + '=' + value.b + "\0" }.join
    body << "\0"
    raise Invalid, 'environment capacity exceeded' if body.bytesize > SIZE - 4
    body = body.ljust(SIZE - 4, parsed[:pad_byte].chr)
    [Zlib.crc32(body)].pack('V') + body
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    raise A133Env::Invalid, 'usage: a133-env-edit.rb INPUT_COPY NEW_OUTPUT NAME=VALUE ...' unless ARGV.size >= 3
    input, output, *arguments = ARGV
    raise A133Env::Invalid, 'output must be a new regular-file path' if File.exist?(output) || File.symlink?(output)
    updates = {}
    arguments.each do |argument|
      name, value = argument.split('=', 2)
      raise A133Env::Invalid, 'invalid or duplicate update' unless value && !updates.key?(name)
      updates[name] = value
    end
    image = A133Env.patch(File.binread(input), updates)
    File.open(output, File::WRONLY | File::CREAT | File::EXCL, 0600) { |file| file.write(image) }
    puts 'Environment copy prepared and CRC verified; no device writes performed'
  rescue A133Env::Invalid, SystemCallError => error
    warn error.message
    exit 1
  end
end
