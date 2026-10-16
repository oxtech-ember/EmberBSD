# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Exercise the actual flex filter argument boundary.
require 'fileutils'
require 'open3'
require 'shellwords'

abort 'Usage: flex-filter-sentinel.rb SOURCE_ROOT NEW_OUTPUT' unless ARGV.length == 2
root = File.realpath(ARGV[0])
output = File.expand_path(ARGV[1])
abort 'new absolute output required' unless ARGV[1].start_with?('/') && !File.exist?(output)
Dir.mkdir(output)
base = root + '/external/bsd/flex/dist/src'
source = File.read(base + '/filter.c')
start = source.index(/^struct filter \*filter_create_ext /) or abort 'filter constructor missing'
opening = source.index('{', start)
depth = 1
finish = opening + 1
while depth > 0
  case source[finish]
  when '{' then depth += 1
  when '}' then depth -= 1
  end
  finish += 1
  abort 'unclosed filter constructor' if finish >= source.length
end
constructor = source[start...finish]
record = File.read(base + '/flexdef.h')[/^struct filter \{.*?^\};/m] or abort 'filter record missing'
call = File.read(base + '/main.c')[/^\s*filter_create_ext\(output_chain, m4,.*?;/] or abort 'external filter call missing'
fixture = source[0...source.index('#include')] + <<~C
  #include <assert.h>
  #include <stdarg.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #{record}
  #define _(s) (s)
  static void flexerror(const char *text) { fputs(text, stderr); abort(); }
  /* Enforce the documented pointer-sentinel contract at the real call site. */
  extern struct filter *filter_create_ext(struct filter *chain, const char *cmd, ...)
      __attribute__((sentinel));
  #{constructor}
  int main(void) {
      for (unsigned i = 0; i < 1000; i++) {
          struct filter anchor = {0};
          struct filter *output_chain = &anchor;
          const char *m4 = "/native/m4";
          #{call}
          struct filter *f = anchor.next;
          assert(f != NULL && f->argc == 2);
          assert(f->argv[0] == m4 && strcmp(f->argv[1], "-P") == 0);
          assert(f->argv[2] == NULL && f->next == NULL);
          free(f->argv);
          free(f);
      }
      puts("PASS: actual flex filter call produces exactly m4, -P and a null pointer");
      return 0;
  }
C
path = output + '/filter-sentinel.c'
File.write(path, fixture)
cc = Shellwords.split(ENV.fetch('CC', 'cc'))
flags = Shellwords.split(ENV.fetch('CFLAGS', '-O2 -std=gnu17'))
cppflags = Shellwords.split(ENV.fetch('CPPFLAGS', ''))
ldflags = Shellwords.split(ENV.fetch('LDFLAGS', ''))
command = [*cc, *cppflags, *flags, '-Wall', '-Wextra', '-Werror', path, *ldflags, '-o', output + '/filter-sentinel']
text, status = Open3.capture2e(*command)
File.write(output + '/compile.log', text)
abort text unless status.success?
text, status = Open3.capture2e(output + '/filter-sentinel')
File.write(output + '/run.log', text)
abort text unless status.success?
puts text
