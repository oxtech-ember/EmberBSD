#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD. Execute the actual PRIME map and native fd-mmap bodies.
require 'fileutils'
require 'open3'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
prime = File.read(root + '/sys/external/bsd/drm2/dist/drm/drm_prime.c')
native = File.read(root + '/sys/external/bsd/drm2/linux/linux_dma_buf.c')
map = prime[/^struct sg_table \*drm_gem_map_dma_buf\(.*?^}\n/m] or abort 'map body missing'
mmap = native[/^static int\ndmabuf_fop_mmap\(.*?^}\n/m] or abort 'mmap body missing'
Dir.mktmpdir('drm-prime-map-', ENV.fetch('TMPDIR', '/tmp')) do |work|
 File.write(work+'/bodies.h', map + mmap)
 command = [ENV.fetch('CC','cc'), '-std=c11', '-g', '-Wall', '-Wextra', '-Werror',
   '-Wno-unused-parameter', '-Wno-sign-compare', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
   '-I'+work, __dir__+'/drm-prime-map-contract.c', '-o', work+'/check']
 abort 'compile failed' unless system(*command)
 abort 'PRIME map contract failed' unless system(work+'/check')
end
