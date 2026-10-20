#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Export complete production bodies for controlled actual-source job fixtures.
source, output = ARGV
abort 'usage: source-tree output-directory' unless source && output && ARGV.size == 2
strip = ->(text) { text.lines.reject { |line| line.start_with?('#include ') }.join }
base = source + '/sys/external/bsd/drm2/'
files = %w[h c].to_h do |ext|
  name = ext == 'h' ? 'resv-types.h' : 'resv-body.h'
  [name, strip.call(File.read(base + 'bcmv3d/bcmv3d_resv.' + ext))]
end
files['heapsort-body.h'] = strip.call(File.read(source + '/common/lib/libc/stdlib/heapsort.c'))
gem = File.read(base + 'dist/drm/drm_gem.c')
functions = %w[drm_gem_lock_reservations drm_gem_unlock_reservations].map do |name|
  matches = gem.scan(/^(?:int|void)\n#{name}\(.*?^}/m)
  abort 'GEM function boundary: ' + name unless matches.size == 1
  matches.first
end
files['gem-lock-body.h'] = gem.split(/^#include /, 2).first + functions.join("\n")
abort 'existing export' if files.keys.any? { |name| File.exist?(output + '/' + name) }
files.each { |name, bytes| File.write(output + '/' + name, bytes) }
