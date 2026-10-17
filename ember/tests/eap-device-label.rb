#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Exercise the actual audio label and getdev callback.
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'

abort 'Usage: eap-device-label.rb [SOURCE_ROOT [NEW_OUTPUT]]' unless ARGV.length <= 2
root = File.expand_path(ARGV[0] || '../..', ARGV[0] ? Dir.pwd : __dir__)
source = File.read(root + '/sys/dev/pci/eap.c')
header = File.read(root + '/sys/sys/audioio.h')
length = header[/^#define MAX_AUDIO_DEV_LEN\s+\d+$/] or abort 'Missing audio label length'
type = header[/typedef struct audio_device \{.*?\} audio_device_t;/m] or abort 'Missing audio device type'
label = source[/^static struct audio_device eap_device = \{.*?^\};/m] or abort 'Missing EAP label'
getter = source[/^static int\neap_getdev\([^\n]*\)\n\{.*?^\}/m] or abort 'Missing EAP getdev callback'
fixture = <<~C
  #include <assert.h>
  #include <stddef.h>
  #include <string.h>
  #{length}
  #{type}
  #{label}
  #{getter}
  _Static_assert(sizeof(struct audio_device) == 48, "audio ioctl ABI size");
  _Static_assert(offsetof(struct audio_device, version) == 16, "version ABI offset");
  _Static_assert(offsetof(struct audio_device, config) == 32, "config ABI offset");
  int main(void) {
    struct audio_device device;
    memset(&device, 0xff, sizeof(device));
    int status = eap_getdev(NULL, &device);
    assert(status == 0);
    assert(memchr(device.name, 0, sizeof(device.name)) != NULL);
    assert(memchr(device.version, 0, sizeof(device.version)) != NULL);
    assert(memchr(device.config, 0, sizeof(device.config)) != NULL);
    assert(strcmp(device.name, "EnsoniqAudioPCI") == 0);
    assert(strcmp(device.version, "") == 0);
    assert(strcmp(device.config, "eap") == 0);
    return 0;
  }
C
run = lambda do |dir|
  file = dir + '/fixture.c'
  File.write(file, fixture)
  gcc = Shellwords.split(ENV.fetch('HOST_GCC16', 'gcc-16'))
  version, err, status = Open3.capture3(*gcc, '-dumpfullversion')
  abort "Select host GCC16: #{version}#{err}" unless status.success? && version.start_with?('16.')
  native = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  commands = {
    'gcc16' => [*gcc, *Shellwords.split(ENV.fetch('HOST_GCC16_FLAGS', '')), '-O2', '-std=c17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fanalyzer', '-c', file, '-o', dir + '/fixture.o'],
    'sanitize-build' => [*native, *Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O1 -g -std=c17 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer')), file, '-o', dir + '/fixture'],
    'sanitize-run' => [dir + '/fixture']
  }
  File.write(dir + '/commands.json', JSON.pretty_generate(commands) + "\n")
  commands.each do |name, command|
    out, err, status = Open3.capture3(*command)
    File.write(dir + '/' + name + '.log', out + err)
    abort "FAIL: #{name}: #{out}#{err}" unless status.success?
  end
  puts 'PASS: actual EAP getdev returns three bounded NUL-terminated fields with unchanged ioctl ABI'
end
if ARGV[1]
  output = File.expand_path(ARGV[1])
  abort 'Output must be a new directory' if File.exist?(output) || File.symlink?(output)
  Dir.mkdir(output)
  run.call(output)
else
  Dir.mktmpdir('ember-eap-label-', &run)
end
