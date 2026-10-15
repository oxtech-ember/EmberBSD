# Origin: EmberBSD sysinst keyboard-hook contract, 2026-10-15.
# SPDX-License-Identifier: BSD-2-Clause
require 'fileutils'
require 'open3'
require 'optparse'
require 'shellwords'
require 'tmpdir'

options = {}
OptionParser.new do |parser|
  parser.on('--compile-only', 'Compile for a target without running it') { options[:compile_only] = true }
  parser.on('--output-dir DIR', 'Keep generated sources and outputs') { |dir| options[:output] = File.expand_path(dir) }
end.parse!
abort 'unexpected arguments' unless ARGV.empty?

root = File.expand_path('../..', __dir__)
source = File.read(root + '/usr.sbin/sysinst/defs.h')
blocks = source.scan(/^#ifdef WSKBD\n.*?^#endif$/m).select { |block| block.include?('get_kb_encoding') }
abort 'expected one keyboard-hook block in defs.h' unless blocks.length == 1
license = source.split('#ifndef _DEFS_H_', 2).first
code = <<~C
  #include <assert.h>
  #include "keyboard-hooks.h"

  static unsigned int get_calls, save_calls, conditions, alternatives;

  #ifdef WSKBD
  void
  get_kb_encoding(void)
  {
  \tget_calls++;
  }

  void
  save_kb_encoding(void)
  {
  \tsave_calls++;
  }
  #endif

  static int
  condition(int value)
  {
  \tconditions++;
  \treturn value;
  }

  int
  main(void)
  {
  \tint i;

  \tget_kb_encoding();
  \tsave_kb_encoding();
  \tfor (i = 0; i < 2; i++) {
  \t\tif (condition(i))
  \t\t\tget_kb_encoding();
  \t\tif (condition(i))
  \t\t\tsave_kb_encoding();
  \t\tif (condition(i))
  \t\t\tget_kb_encoding();
  \t\telse
  \t\t\talternatives++;
  \t\tif (condition(i))
  \t\t\tsave_kb_encoding();
  \t\telse
  \t\t\talternatives++;
  \t}
  \tassert(conditions == 8);
  \tassert(alternatives == 2);
  #ifdef WSKBD
  \tassert(get_calls == 3 && save_calls == 3);
  #else
  \tassert(get_calls == 0 && save_calls == 0);
  #endif
  \treturn 0;
  }
C
run = lambda do |dir|
  File.write(dir + '/keyboard-hooks.h', license + blocks.first + "\n")
  File.write(dir + '/keyboard.c', code)
  [false, true].each do |wskbd|
    name = wskbd ? 'wskbd' : 'no-wskbd'
    command = Shellwords.split(ENV.fetch('CC', 'cc')) +
      %w[-std=c11 -Wall -Wextra -Werror -Wempty-body] +
      Shellwords.split(ENV.fetch('CFLAGS', ''))
    command << '-UNDEBUG'
    command << '-DWSKBD' if wskbd
    command += ['-c'] if options[:compile_only]
    output = dir + '/' + name + (options[:compile_only] ? '.o' : '')
    command += [dir + '/keyboard.c', '-o', output]
    command += Shellwords.split(ENV.fetch('LDFLAGS', '')) unless options[:compile_only]
    out, err, status = Open3.capture3(*command)
    abort out + err unless status.success?
    unless options[:compile_only]
      out, err, status = Open3.capture3(output)
      abort out + err unless status.success?
    end
    puts "PASS: #{name} keyboard hooks #{options[:compile_only] ? 'compile' : 'compile and run'}"
  end
end
if options[:output]
  FileUtils.mkdir_p(options[:output])
  run.call(options[:output])
else
  Dir.mktmpdir('sysinst-keyboard-') { |dir| run.call(dir) }
end
