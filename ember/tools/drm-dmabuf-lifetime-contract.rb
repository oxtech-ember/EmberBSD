#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD. Actual dma-buf export/fd/put and GEM export-cache lifetime.
require 'tmpdir'
root=File.expand_path('../..',__dir__)
native=File.read(root+'/sys/external/bsd/drm2/linux/linux_dma_buf.c')
gem=File.read(root+'/sys/external/bsd/drm2/dist/drm/drm_gem.c')
prime=File.read(root+'/sys/external/bsd/drm2/dist/drm/drm_prime.c')
parts=[native[/^struct dma_buf \*\ndma_buf_export\(.*?^}\n/m],
 native[/^int\ndma_buf_fd\(.*?^}\n/m],
 native[/^void\nget_dma_buf\(.*?^}\n/m],native[/^void\ndma_buf_put\(.*?^}\n/m],
 native[/^static int\ndmabuf_fop_close\(.*?^}\n/m],
 gem[/^static void drm_gem_object_exported_dma_buf_free\(.*?^}\n/m],
 prime[/^struct dma_buf \*drm_gem_dmabuf_export\(.*?^}\n/m],
 prime[/^void drm_gem_dmabuf_release\(.*?^}\n/m]]
abort 'actual lifetime body missing' if parts.any?(&:nil?)
Dir.mktmpdir('drm-dmabuf-life-',ENV.fetch('TMPDIR','/tmp')) do |work|
 File.write(work+'/bodies.h',parts.join("\n"))
 abort 'compile failed' unless system(ENV.fetch('CC','cc'),'-std=c11','-g','-pthread',
  '-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',
  '-fno-sanitize-recover=all','-I'+work,__dir__+'/drm-dmabuf-lifetime-contract.c','-o',work+'/check')
 abort 'lifetime contract failed' unless system(work+'/check')
end
