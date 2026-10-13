#!/usr/bin/env ruby
# Origin: EmberBSD - streaming integrity check for saved YS-M33 eMMC backups.
require 'digest'
require 'json'
require 'optparse'
require 'timeout'
require 'zlib'

module A133Backup
  class Invalid < StandardError; end
  BYTES = 31037849600
  INVENTORY = [
    ['bootloader', 73728, 65536], ['env', 139264, 32768],
    ['boot', 172032, 65536], ['super', 237568, 4194304],
    ['misc', 4431872, 32768], ['recovery', 4464640, 65536],
    ['cache', 4530176, 1572864], ['vbmeta', 6103040, 32768],
    ['vbmeta_system', 6135808, 32768], ['vbmeta_vendor', 6168576, 32768],
    ['metadata', 6201344, 32768], ['private', 6234112, 32768],
    ['frp', 6266880, 1024], ['empty', 6267904, 31744],
    ['dtbo', 6299648, 4096], ['media_data', 6303744, 262144],
    ['UDISK', 6565888, 54054879]
  ].each_with_index.map do |entry, index|
    {index: index + 1, name: entry[0], start: entry[1], sectors: entry[2]}.freeze
  end.freeze
  module_function

  def header(block, current, alternate, count)
    raise Invalid, 'gpt_signature_missing' unless block && block.bytesize == 512 &&
      block.byteslice(0, 8) == 'EFI PART'
    size = block.byteslice(12, 4).unpack1('V')
    raise Invalid, 'gpt_header_layout_mismatch' unless (92..512).cover?(size) &&
      block.byteslice(8, 4).unpack1('V') == 0x10000 && block.byteslice(20, 4).unpack1('V') == 0
    copy = block.byteslice(0, size).dup
    copy[16, 4] = "\0" * 4
    raise Invalid, 'gpt_header_crc_mismatch' unless Zlib.crc32(copy) == block.byteslice(16, 4).unpack1('V')
    values = block.byteslice(24, 32).unpack('Q<4')
    array_lba = block.byteslice(72, 8).unpack1('Q<')
    raise Invalid, 'gpt_header_layout_mismatch' unless values.first(2) == [current, alternate] &&
      block.byteslice(80, 4).unpack1('V') == count && count.between?(1, 128) &&
      block.byteslice(84, 4).unpack1('V') == 128
    {first: values[2], last: values[3], guid: block.byteslice(56, 16),
     array_lba: array_lba, array_crc: block.byteslice(88, 4).unpack1('V')}
  end

  def validate_gpt(head, tail, bytes, inventory)
    sectors = bytes / 512
    mbr = head.byteslice(446, 64)
    raise Invalid, 'protective_mbr_invalid' unless head.byteslice(510, 2) == "\x55\xaa".b &&
      mbr.getbyte(4) == 0xee && mbr.byteslice(8, 4).unpack1('V') == 1 &&
      [sectors - 1, 0xffffffff].include?(mbr.byteslice(12, 4).unpack1('V')) &&
      mbr.byteslice(16, 48) == "\0" * 48
    a = header(head.byteslice(512, 512), 1, sectors - 1, inventory.size)
    b = header(tail.byteslice(-512, 512), sectors - 1, 1, inventory.size)
    array_bytes = inventory.size * 128
    array_sectors = (array_bytes + 511) / 512
    raise Invalid, 'gpt_header_layout_mismatch' unless a[:array_lba] == 2 &&
      a[:first] >= 2 + array_sectors && a[:first] <= a[:last] &&
      a[:last] < sectors - 1 && b[:array_lba] >= sectors - 34 &&
      b[:array_lba] > b[:last] && b[:array_lba] + array_sectors <= sectors - 1
    raise Invalid, 'gpt_copies_differ' unless a.values_at(:first, :last, :guid) ==
      b.values_at(:first, :last, :guid) && a[:guid].bytes.any? { |value| value != 0 }
    primary = head.byteslice(1024, array_bytes)
    secondary = tail.byteslice((b[:array_lba] - (sectors - 34)) * 512, array_bytes)
    raise Invalid, 'gpt_array_crc_mismatch' unless primary && secondary &&
      primary.bytesize == array_bytes && secondary.bytesize == array_bytes &&
      Zlib.crc32(primary) == a[:array_crc] && Zlib.crc32(secondary) == b[:array_crc]
    raise Invalid, 'gpt_copies_differ' unless primary == secondary
    unique = []
    previous_end = a[:first] - 1
    inventory.each_with_index do |part, index|
      entry = primary.byteslice(index * 128, 128)
      start, last = entry.byteslice(32, 16).unpack('Q<2')
      name = entry.byteslice(56, 72).force_encoding('UTF-16LE').encode('UTF-8').split("\0").first
      guid = entry.byteslice(16, 16)
      raise Invalid, 'partition_layout_mismatch' unless part[:index] == index + 1 &&
        entry.byteslice(0, 16).bytes.any? { |value| value != 0 } &&
        guid.bytes.any? { |value| value != 0 } && !unique.include?(guid) &&
        start == part[:start] && last - start + 1 == part[:sectors] && name == part[:name] &&
        start > previous_end && start >= a[:first] && last <= a[:last]
      unique << guid
      previous_end = last
    end
    true
  rescue EncodingError
    raise Invalid, 'partition_layout_mismatch'
  end

  def verify_stream(io, expected_bytes, expected_sha, inventory)
    bytes = 0
    head = ''.b
    tail = ''.b
    digest = Digest::SHA256.new
    ranges = inventory.select { |part| %w[bootloader env boot recovery].include?(part[:name]) }.map do |part|
      {name: part[:name], start: part[:start] * 512,
       size: part[:sectors] * 512, hash: Digest::SHA256.new}
    end
    while (chunk = io.read(1048576))
      raise Invalid, 'disk_size_mismatch' if bytes + chunk.bytesize > expected_bytes
      digest.update(chunk)
      head << chunk.byteslice(0, [17408 - head.bytesize, chunk.bytesize].min) if head.bytesize < 17408
      tail = (tail + chunk).byteslice(-[tail.bytesize + chunk.bytesize, 17408].min, 17408)
      ranges.each do |range|
        first = [bytes, range[:start]].max
        last = [bytes + chunk.bytesize, range[:start] + range[:size]].min
        range[:hash].update(chunk.byteslice(first - bytes, last - first)) if last > first
      end
      bytes += chunk.bytesize
    end
    raise Invalid, 'disk_size_mismatch' unless bytes == expected_bytes && expected_bytes >= 34816 &&
      expected_bytes % 512 == 0
    raise Invalid, 'disk_sha256_mismatch' unless digest.hexdigest == expected_sha
    validate_gpt(head, tail, bytes, inventory)
    hashes = ranges.each_with_object({}) { |range, result| result[range[:name]] = range[:hash].hexdigest }
    {bytes: bytes, uncompressed_sha256: digest.hexdigest, partition_sha256: hashes,
     gpt_sha256: Digest::SHA256.hexdigest(head + tail),
     gpt_headers_crc: true, gpt_arrays_crc: true, partition_layout_verified: true}
  end

  class Decoder
    def initialize(output, pid, errors)
      @output, @pid, @errors = output, pid, errors
      @finished = false
    end
    def read(size)
      value = @output.read(size)
      if value.nil? && !@finished
        _, status = Process.wait2(@pid)
        noisy = @errors.value
        @finished = true
        raise Invalid, 'decompression_failed' unless status.success? && !noisy
      end
      value
    end
  end

  def decode(source, executable)
    output, output_write = IO.pipe
    errors, errors_write = IO.pipe
    pid = Process.spawn(executable, '-d', '-q', '-c', in: source, out: output_write,
      err: errors_write, pgroup: true)
    output_write.close
    errors_write.close
    drain = Thread.new do
      noisy = false
      begin
        while errors.read(16384)
          noisy = true
        end
      rescue IOError
        # Cleanup closes this descriptor after killing a timed-out decoder.
      end
      noisy
    end
    yield Decoder.new(output, pid, drain)
  ensure
    if pid
      begin
        Process.kill('KILL', -pid)
      rescue Errno::ESRCH
        # The decoder and its group have already exited.
      end
      begin
        Process.wait(pid)
      rescue Errno::ECHILD
        # EOF processing already reaped the decoder.
      end
    end
    [output, output_write, errors, errors_write].compact.each { |io| io.close unless io.closed? }
    drain.join if drain
  end

  def verify_file(path, sha256:, format: 'zstd', zstd: 'zstd', timeout: 3600)
    raise Invalid, 'invalid_arguments' unless path.is_a?(String) &&
      %w[zstd raw].include?(format) && timeout.is_a?(Integer) && (1..7200).cover?(timeout) &&
      sha256.is_a?(String) && sha256.match?(/\A[0-9a-f]{64}\z/) &&
      zstd.is_a?(String) && !zstd.empty?
    path, sha256 = path.dup.freeze, sha256.dup.freeze
    raise Invalid, 'backup_file_not_regular' unless File.lstat(path).file?
    verified = File.open(path, File::RDONLY | File::NOFOLLOW | File::NONBLOCK) do |file|
      before = file.stat
      raise Invalid, 'backup_file_not_regular' unless before.file?
      receipt = Timeout.timeout(timeout) do
        if format == 'raw'
          raise Invalid, 'disk_size_mismatch' unless before.size == BYTES
          verify_stream(file, BYTES, sha256, INVENTORY)
        else
          decode(file, zstd) { |stream| verify_stream(stream, BYTES, sha256, INVENTORY) }
        end
      end
      after = file.stat
      raise Invalid, 'backup_source_changed' unless [:dev, :ino, :size, :mtime, :ctime].all? do |field|
        before.public_send(field) == after.public_send(field)
      end
      receipt
    end
    verified.merge(status: 'backup_integrity_verified', writes_performed: 0,
      installation_ready: false, filesystem_consistency: 'not_established_by_integrity_check')
  rescue Invalid => error
    raise Invalid, error.message, cause: nil
  rescue Timeout::Error
    raise Invalid, 'backup_read_timeout', cause: nil
  rescue SystemCallError, IOError
    raise Invalid, 'backup_read_failed', cause: nil
  rescue ArgumentError
    raise Invalid, 'invalid_arguments', cause: nil
  end
end

if $PROGRAM_NAME == __FILE__
  result = {writes_performed: 0, installation_ready: false}
  begin
    options = {format: 'zstd', zstd: 'zstd', timeout: 3600}
    OptionParser.new do |parser|
      parser.on('--sha256 HASH') { |value| options[:sha] = value }
      parser.on('--format FORMAT') { |value| options[:format] = value }
      parser.on('--zstd PATH') { |value| options[:zstd] = value }
      parser.on('--timeout SECONDS', Integer) { |value| options[:timeout] = value }
    end.parse!
    raise A133Backup::Invalid, 'invalid_arguments' unless ARGV.size == 1
    result.merge!(A133Backup.verify_file(ARGV.first, sha256: options[:sha],
      format: options[:format], zstd: options[:zstd], timeout: options[:timeout]))
    puts JSON.pretty_generate(result)
  rescue A133Backup::Invalid, Timeout::Error, OptionParser::ParseError, SystemCallError, IOError => error
    result[:status] = 'inspection_stopped'
    result[:reason] = if error.is_a?(A133Backup::Invalid)
      error.message
    elsif error.is_a?(Timeout::Error)
      'backup_read_timeout'
    elsif error.is_a?(OptionParser::ParseError)
      'invalid_arguments'
    else
      'backup_read_failed'
    end
    puts JSON.pretty_generate(result)
    exit 1
  end
end
