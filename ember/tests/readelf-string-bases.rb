#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Real ELF fixtures: explicit bases, inline strings and indirect forms.
require 'fileutils'
require 'open3'
require 'timeout'
cc, reader, work = ARGV
abort 'usage: readelf-string-bases.rb CROSS_CC READELF NEW_WORK' unless ARGV.size == 3
abort 'NEW_WORK exists' if File.exist?(work)
FileUtils.mkdir_p(work)
passed = failed = 0
[32, 64].each do |width|
  [false, true].each do |split|
    %w[late inline indirect duplicate unsupported zero indirect-zero].each do |kind|
      name = "w#{width}-#{split ? 'split' : 'plain'}-#{kind}"
      suffix = split ? '.dwo' : ''
      offset = width == 32 ? '.long' : '.quad'
      header = width == 32 ? '.long unit_end-unit_body\nunit_body:' : '.long 0xffffffff\n.quad unit_end-unit_body\nunit_body:'
      table_header = width == 32 ? '.long 12\n.short 5,0' : '.long 0xffffffff\n.quad 20\n.short 5,0'
      base = width == 32 ? 24 : 48 # second contribution's first offset
      zero = kind.end_with?('zero')
      base = 0 if zero
      table_header = '' if zero # compatibility layout, not normative DWARF5
      base_form = {'indirect'=>0x16, 'indirect-zero'=>0x16, 'unsupported'=>0x06}.fetch(kind, 0x17)
      root_attrs = kind == 'inline' ? '.uleb128 3,8\n' : ''
      root_attrs += '.uleb128 0x25,0x25\n.uleb128 0x72,' + base_form.to_s + '\n'
      root_attrs += '.uleb128 0x72,0x17\n' if kind == 'duplicate'
      root_values = kind == 'inline' ? '.asciz "INLINE"\n' : ''
      root_values += '.byte 0\n'
      root_values += '.uleb128 0x17\n' if kind.start_with?('indirect')
      root_values += kind == 'unsupported' ? '.long 0x12345678\n' : "#{offset} #{kind == 'duplicate' ? (width == 32 ? 8 : 16) : base}\n"
      root_values += "#{offset} #{base}\n" if kind == 'duplicate'
      # Unsupported form is deliberately malformed. Its skip must not consume
      # the next DIE; split uses the default contribution, plain base stays 0.
      expected = kind == 'unsupported' ? (split ? 'FIRST_TYPE' : nil) : (zero ? 'FIRST_TYPE' : 'SECOND_TYPE')
      asm = <<~ASM
        .section .debug_abbrev#{suffix},"",%progbits
        .uleb128 1,0x11
        .byte 1
        #{root_attrs}
        .uleb128 0,0
        .uleb128 2,0x24
        .byte 0
        .uleb128 3,0x25,0x0b,0x0b,0,0
        .byte 0
        .section .debug_info#{suffix},"",%progbits
        #{header}
        .short 5
        .byte #{split ? 5 : 1},8
        #{offset} 0
        #{split ? '.quad 0x12345678' : ''}
        .uleb128 1
        #{root_values}
        .uleb128 2
        .byte 1,8,0
        unit_end:
        .section .debug_str#{suffix},"MS",%progbits,1
        str_start:
        first: .asciz "FIRST"
        first_type: .asciz "FIRST_TYPE"
        second: .asciz "SECOND"
        second_type: .asciz "SECOND_TYPE"
        .section .debug_str_offsets#{suffix},"",%progbits
        #{table_header}
        #{offset} first-str_start,first_type-str_start
        #{table_header}
        #{offset} second-str_start,second_type-str_start
      ASM
      asm = asm.gsub('\\n', "\n")
      input = File.join(work, "#{name}.s")
      object = File.join(work, "#{name}#{split ? '.dwo' : '.o'}")
      File.write(input, asm)
      out, err, status = Open3.capture3(cc, '-c', '-x', 'assembler', input, '-o', object)
      abort "assemble #{name}: #{out}#{err}" unless status.success?
      out, err, status = Timeout.timeout(15) { Open3.capture3(reader, '--debug-dump=info', object) }
      File.write(File.join(work, "#{name}.info"), out)
      File.write(File.join(work, "#{name}.err"), err)
      child = out.split('DW_TAG_base_type)', 2)[1].to_s
      ok = status.success? && child.match?(/DW_AT_byte_size\s*:\s*8\s*$/)
      ok &&= child.match?(/DW_AT_name.*:\s*#{expected}\s*$/) if expected
      if kind == 'duplicate'
        ok &&= err.include?('multiple str_offsets_base')
      elsif kind == 'unsupported'
        ok &&= err.include?('Unexpected form of DW_AT_str_offsets_base')
      else
        ok &&= err.empty?
        producer = zero ? 'FIRST' : 'SECOND'
        ok &&= out.match?(/DW_AT_producer.*:\s*#{producer}\s*$/)
      end
      if ok
        passed += 1; puts "PASS #{name}"
      else
        failed += 1; puts "FAIL #{name}"
      end
    end
  end
end
puts "readelf explicit string bases: #{passed} passed, #{failed} failed (28 actual ELF cases; zero uses a compatibility layout)"
exit(failed.zero? ? 0 : 1)
