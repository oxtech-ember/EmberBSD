#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Prepare unchanged Mesa CPU offset helpers for the finite native TFU gate.
require 'digest'
require 'fileutils'
require 'open3'

module TfuOracle
  ARCHIVE_SHA = 'bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9'.freeze
  INPUTS = {
    'src/broadcom/common/v3d_tiling.c' => 'b481709b5bf5c6f7609bb7c49d1dd66098ccddf955fd8fe3d3f10c2f4ee5f4f3',
    'src/broadcom/common/v3d_tfu.h' => '7167dd4b1dadb5d1b37f7ba575093d713c52f2fbdb3d0a5482658139c57e1a59'
  }.freeze
  FUNCTIONS = {
    'v3d_utile_width' => "uint32_t\nv3d_utile_width(int cpp)\n{",
    'v3d_utile_height' => "uint32_t\nv3d_utile_height(int cpp)\n{",
    'v3d_get_utile_pixel_offset' => "static inline uint32_t\nv3d_get_utile_pixel_offset(",
    'v3d_get_uif_pixel_offset' => "static inline uint32_t\nv3d_get_uif_pixel_offset(",
    'v3d_get_uif_no_xor_pixel_offset' => "static inline uint32_t\nv3d_get_uif_no_xor_pixel_offset("
  }.freeze

  class Error < StandardError; end

  def self.check(condition, message)
    raise Error, message unless condition
  end

  def self.read_sources(input)
    input = File.realpath(input)
    archive = !File.directory?(input)
    check(File.file?(input) || !archive, 'Mesa input must be a source directory or archive')
    check(!archive || Digest::SHA256.file(input).hexdigest == ARCHIVE_SHA, 'Mesa archive SHA256 mismatch')
    sources = INPUTS.to_h do |path, expected|
      if archive
        text, error, status = Open3.capture3('tar', '-xJOf', input, "mesa-26.2.4/#{path}")
        check(status.success?, "Mesa archive member failed: #{path}: #{error}")
      else
        full = File.join(input, path)
        check(File.file?(full), "Mesa source missing: #{path}")
        text = File.binread(full)
      end
      check(Digest::SHA256.hexdigest(text) == expected, "Mesa source SHA256 mismatch: #{path}")
      [path, text]
    end
    [input, archive, sources]
  end

  def self.function(source, name, marker)
    check(source.scan(Regexp.new(Regexp.escape(marker))).length == 1, "Mesa function boundary changed: #{name}")
    first = source.index(marker)
    opening = source.index('{', first)
    check(!opening.nil?, "Mesa function has no body: #{name}")
    depth = 0
    closing = nil
    # The exact SHA-pinned helpers have no braces inside strings or comments.
    source[opening..].each_char.with_index(opening) do |character, at|
      depth += 1 if character == '{'
      depth -= 1 if character == '}'
      if depth.zero?
        closing = at
        break
      end
    end
    check(!closing.nil?, "Mesa function has no closing boundary: #{name}")
    source[first..closing] + "\n"
  end

  def self.prepare(input, output)
    resolved, archive, sources = read_sources(input)
    tiling = sources.fetch('src/broadcom/common/v3d_tiling.c')
    license = tiling[/\A\/\*.*?\*\//m]
    check(license && license.include?('Copyright') && license.include?('Permission is hereby granted'), 'Mesa license boundary changed')
    header = license + <<~HEADER

      /* Generated from Mesa 26.2.4. Do not edit or use another tile algorithm. */
      #ifndef EMBER_TFU_MESA_ORACLE
      #define EMBER_TFU_MESA_ORACLE
      #include <assert.h>
      #include <stdbool.h>
      #include <stdint.h>
      #include <stdlib.h>
      /* Explicit integer/compiler services for the unchanged scalar bodies. */
      #define align(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
      #define ffs(value) __builtin_ffs(value)
      #define UNREACHABLE(message) abort()
    HEADER
    FUNCTIONS.each { |name, marker| header += function(tiling, name, marker) + "\n" }
    header += sources.fetch('src/broadcom/common/v3d_tfu.h') + "\n"
    header += "#undef UNREACHABLE\n#undef ffs\n#undef align\n#endif\n"
    check(!File.exist?(output) && !File.symlink?(output), 'Oracle output already exists')
    # Validate the original inputs before publishing the generated header.
    _, _, after = read_sources(resolved)
    check(sources == after, 'Mesa sources changed during extraction')
    File.open(output, File::WRONLY | File::CREAT | File::EXCL) { |file| file.write(header) }
    { input: resolved, kind: archive ? 'upstream archive' : 'pinned source directory',
      source_pins: INPUTS, header_sha256: Digest::SHA256.hexdigest(header) }
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    TfuOracle.check(ARGV.length == 2, 'usage: bcm2712-v3d-tfu-oracle.rb MESA_SOURCE_OR_ARCHIVE NEW_HEADER')
    result = TfuOracle.prepare(File.expand_path(ARGV[0]), File.expand_path(ARGV[1]))
    puts "PASS: pinned Mesa CPU oracle #{result.fetch(:header_sha256)}"
  rescue TfuOracle::Error, SystemCallError => error
    warn "FAIL: #{error.message}"
    exit 1
  end
end
