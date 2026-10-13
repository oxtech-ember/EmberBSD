# SPDX-License-Identifier: BSD-2-Clause
# Apple reserves x18. Imported ARM64 arithmetic must not use it.
require 'open3'
source = File.expand_path('../../external/lgpl3/gmp/dist/mpn/arm64', __dir__)
Dir.glob(File.join(source, '*.asm')).each do |path|
  File.readlines(path).each_with_index do |line, i|
    next if line.match?(/\A\s*(?:C\s|dnl\b)/)
    abort "FAIL: reserved register in #{File.basename(path)}:#{i + 1}" if line.match?(/\b[wx]18\b/)
  end
end
puts 'PASS: ARM64 GMP sources preserve the Apple x18 platform register'
ARGV.each do |object|
  text, error, status = Open3.capture3('otool', '-tv', object)
  abort error unless status.success?
  abort "FAIL: assembled object uses reserved x18: #{object}" if text.match?(/\b[wx]18\b/)
end
puts 'PASS: inspected Darwin object instructions preserve x18' unless ARGV.empty?
