#!/usr/bin/env ruby
# Android v2 boot container for the vendor A133 loader.
# Use the ARM64 Image produced by make (netbsd.img), not netbsd.bin or ELF.
abort "usage: #{$PROGRAM_NAME} IMAGE RAMDISK DTB OUTPUT" unless ARGV.size == 4
page = 2048
kernel, ramdisk, dtb = ARGV.first(3).map { |p| File.binread(p) }
abort 'kernel needs an ARM64 Image header (use netbsd.img)' unless
  kernel.size >= 64 && kernel[56, 4] == 'ARMd' &&
  kernel[0, 4].unpack1('V') == 0x14000010 &&
  kernel[8, 8].unpack1('Q<') == 0x200000
abort 'ramdisk overlaps DTB at 0x44000000' if ramdisk.size > 16 * 1024 * 1024
pad = ->(data) { data + "\0".b * ((page - data.size % page) % page) }
header = 'ANDROID!'.b
header << [kernel.size, 0x40080000, ramdisk.size, 0x43000000, 0,
           0x40f00000, 0x40000100, page, 2, 0x14000151].pack('V10')
header << "\0".b * (16 + 512 + 32 + 1024)
header << [0].pack('V') << [0].pack('Q<') << [1660].pack('V')
header << [dtb.size].pack('V') << [0x44000000].pack('Q<')
abort 'incorrect Android v2 header size' unless header.size == 1660
image = pad.call(header) + pad.call(kernel) + pad.call(ramdisk) + pad.call(dtb)
abort 'image exceeds the 32 MiB boot partition' if image.size > 32 * 1024 * 1024
File.binwrite(ARGV.last, image)
