# Origin: EmberBSD font-cache host XML parser regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'shellwords'
require 'tmpdir'
abort 'Usage: x11-host-expat.rb nbmake-amd64 xsrc-directory' unless ARGV.size == 2
make, xsrc = ARGV.map { |arg| File.expand_path(arg) }
root = File.expand_path('../..', __dir__)
flags, status = Open3.capture2(make, '-C', root + '/external/mit/xorg/tools/fc-cache',
  "X11SRCDIR=#{xsrc}", '-v', 'HOST_CPPFLAGS')
abort 'Cannot evaluate font-cache flags' unless status.success?
Dir.mktmpdir('ember-x11-expat') do |work|
  fixture = work + '/parse.c'
  File.write(fixture, <<~C)
    #include <assert.h>
    #include <string.h>
    #include <expat.h>
    static void text(void *ctx, const XML_Char *s, int n) {
      assert(n == 1 && s[0] == 'x');
      ++*(int *)ctx;
    }
    static void parse(const char *xml, int size) {
      int seen = 0;
      XML_Parser parser = XML_ParserCreate(NULL);
      assert(parser != NULL);
      XML_SetUserData(parser, &seen);
      XML_SetCharacterDataHandler(parser, text);
      assert(XML_Parse(parser, xml, size, XML_TRUE) == XML_STATUS_OK);
      assert(seen == 1);
      XML_ParserFree(parser);
    }
    int main(void) {
      const char *xml = "<a>x</a>";
      char le[16], be[16];
      for (int i = 0; i < 8; ++i) {
        le[2*i] = be[2*i+1] = xml[i];
        le[2*i+1] = be[2*i] = 0;
      }
      parse(xml, 8); parse(le, 16); parse(be, 16);
      return 0;
    }
  C
  sources = %w[xmlparse.c xmltok.c xmlrole.c random_arc4random_buf.c].map {
    |name| root + '/external/mit/expat/dist/lib/' + name }
  output, status = Open3.capture2e(*Shellwords.split(ENV.fetch('HOST_CC', 'cc')),
    '-std=gnu99', '-I' + root + '/external/mit/expat/lib/libexpat',
    '-I' + root + '/external/mit/expat/dist/lib',
    *Shellwords.split(flags), *sources, fixture, '-lm', '-o', work + '/parse')
  abort "Font-cache XML host link failed:\n#{output}" unless status.success?
  abort 'Host XML UTF-8/UTF-16 byte-order regression' unless system(work + '/parse')
end
puts 'Font-cache host XML: UTF-8, UTF-16LE and UTF-16BE parsing pass'
