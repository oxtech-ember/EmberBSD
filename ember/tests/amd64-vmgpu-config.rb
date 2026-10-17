#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Real config(1) selection, not driver execution.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
abort 'Usage: amd64-vmgpu-config.rb SOURCE NBCONFIG NEW_OUTPUT [CANDIDATE]' unless (3..4).cover?(ARGV.length)
source, config = ARGV.first(2).map { |path| File.realpath(path) }
work = File.expand_path(ARGV[2])
abort 'new absolute output required' unless ARGV[2].start_with?('/') && !File.exist?(work)
candidate = ARGV[3] || 'GENERIC_VMGPU_TRIAL'
abort 'invalid configuration name' unless candidate.match?(/\A[A-Z][A-Z0-9_]*\z/)
FileUtils.mkdir_p(work)
records = []
configure = lambda do |label, name|
  build = work + '/' + label
  path = source + '/sys/arch/amd64/conf/' + name
  command = [config, '-b', build, '-s', source + '/sys', path]
  out, err, status = Open3.capture3(*command)
  File.write(work + '/' + label + '.log', out + err)
  abort label + ': ' + out + err unless status.success?
  files = %w[Makefile ioconf.c opt_virtgpu_virgl.h opt_virtgpu_console.h]
  files.each { |file| abort 'missing config output: ' + file unless File.file?(build + '/' + file) }
  record = {label: label, name: name, argv: command, exit: status.exitstatus,
    generated_sha256: files.to_h { |file| [file, Digest::SHA256.file(build + '/' + file).hexdigest] }}
  records << record
  File.write(work + '/receipt.json', JSON.pretty_generate(records) + "\n")
  [File.read(build + '/Makefile'), File.read(build + '/ioconf.c'), build]
end
stock, stock_attach, stock_dir = configure.call('stock', 'GENERIC')
abort 'stock GENERIC unexpectedly selects native VirtGPU' if stock.match?(/\/virtgpu_autoconf\.c\s*\\?$/) || stock_attach.include?('"virtiodrm"')
abort 'stock GENERIC lost framebuffer VirtGPU' unless stock_attach.include?('"viogpu"')
trial, trial_attach, trial_dir = configure.call('trial', candidate)
objects = %w[virtgpu_autoconf.o virtgpu_dma_arm64.o virtgpu_dma.o linux_virtio.o drm_gem_shmem_helper.o virtgpu_drv.o virtgpu_kms.o virtgpu_gem.o virtgpu_object.o virtgpu_prime.o virtgpu_vq.o virtgpu_fence.o virtgpu_ioctl.o virtgpu_display.o virtgpu_plane.o virtgpu_console.o]
objects.each do |file|
  name = file.sub(/\.o\z/, '.c')
  abort 'trial missing native driver source: ' + name unless trial.match?(/\/#{Regexp.escape(name)}\s*\\?$/)
end
abort 'trial does not select native VirtGPU attachment' unless trial_attach.include?('"virtiodrm"')
abort 'trial retains conflicting framebuffer or FDT console attachment' if trial_attach.include?('"viogpu"') || trial_attach.include?('"virtiodrmfb"')
%w[opt_virtgpu_virgl.h opt_virtgpu_console.h].each do |file|
  abort 'trial enabled unsupported VirGL or FDT console' if File.read(trial_dir + '/' + file).match?(/^#define\s+VIRTGPU_/)
end
stock_options = Dir[stock_dir + '/opt_*.h'].map { |path| File.read(path) }.join
trial_options = Dir[trial_dir + '/opt_*.h'].map { |path| File.read(path) }.join
abort 'stock module autoload baseline differs' unless stock_options.match?(/^#define\s+MODULAR_DEFAULT_AUTOLOAD\b/)
abort 'trial permits unrelated module autoload' if trial_options.match?(/^#define\s+MODULAR_DEFAULT_AUTOLOAD\b/)
abort 'trial unexpectedly enables personal Intel trial' if trial_options.match?(/^#define\s+EMBER_I915_JSL_4E55_TRIAL\b/)
puts 'PASS: real amd64 config selects native VirtGPU in trial only, with 16 objects and no framebuffer conflict'
puts 'PASS: stock attachment remains, VirGL/FDT console/Intel trial stay off, trial module autoload stays off'
puts 'SCOPE: kernel configuration only; no kernel compile, boot, DRM/GBM or Plasma execution'
