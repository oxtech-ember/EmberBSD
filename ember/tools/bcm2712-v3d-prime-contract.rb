#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD. Actual native PRIME mapping boundary and reference tests.
require 'tmpdir'
root=File.expand_path('../..',__dir__)
source=File.read(root+'/sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.c', encoding: 'UTF-8')
body=source[/^static int\nbcmv3d_prime_mmap\(.*?^}\n/m] or abort 'PRIME mmap missing'
%w[prime_handle_to_fd prime_fd_to_handle].each do |field|
 abort "missing #{field}" unless source.include?(".#{field} = drm_gem_#{field},")
end
abort 'unvalidated foreign import hook' if source.match?(/\.gem_prime_import(?:_sg_table)?\s*=/)
abort 'PRIME mmap not wired' unless source.include?('.gem_prime_mmap = bcmv3d_prime_mmap,')
Dir.mktmpdir('bcmv3d-prime-',ENV.fetch('TMPDIR','/tmp')) do |work|
 File.write(work+'/body.h',body)
 abort 'compile failed' unless system(ENV.fetch('CC','cc'),'-std=c11','-g',
  '-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',
  '-fno-sanitize-recover=all','-I'+work,__dir__+'/bcm2712-v3d-prime-contract.c','-o',work+'/test')
 abort 'mapping contract failed' unless system(work+'/test')
end
