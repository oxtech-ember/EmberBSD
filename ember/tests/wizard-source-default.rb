# Origin: EmberBSD guided set-source default regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'open3'
root = File.expand_path('../..', __dir__)
body = File.read(root + '/usr.sbin/sysinst/wizard.c')[/static int\nselect_source\(void\)\n\{.*?^\}/m] or abort 'Missing source controller'
cases = [
  ['/amd64/binary/sets', 'https://example.test/build-abc/amd64/binary/sets'],
  ['', 'https://example.test/build-abc'],
  ['sets', 'https://example.test/build-abc/sets']
]
Dir.mktmpdir do |temporary|
  cases.each do |prefix, expected|
    code = <<~C
      #include <assert.h>
      #include <stdbool.h>
      #include <stdio.h>
      #include <string.h>
      #define STRSIZE 1024
      #define ARCH_SUBDIR "amd64"
      #define SYSINST_HTTP_HOST "example.test"
      #define SYSINST_FTP_DIR "build-abc"
      #define SYSINST_FTP_SET_DIR_BIN #{prefix.dump}
      #define SET_OK 0
      #define SET_RETRY 1
      #define SET_ABANDON 2
      static char source_url[STRSIZE],source_error[STRSIZE],set_dir_bin[STRSIZE],set_dir_src[STRSIZE],requested[STRSIZE];
      static bool net_source;
      static int (*fetch_fn)(const char *);
      static int choose(const char *title,const char *const *options,int count,int selected) {
        (void)title;(void)options;(void)count;(void)selected;return 1;
      }
      static bool input(const char *title,char *value,size_t size) {(void)title;(void)value;(void)size;return true;}
      static int get_via_cdrom(void) {assert(false);return SET_RETRY;}
      static int wizard_network_source(const char *url) {strlcpy(requested,url,sizeof(requested));return SET_OK;}
      #{body}
      int main(void) {
        assert(select_source()==SET_OK);
        if(strcmp(requested,#{expected.dump})) {fprintf(stderr,"Wrong default: %s\\n",requested);return 1;}
        strlcpy(source_url,"http://custom.test/retry-build",sizeof(source_url));
        assert(select_source()==SET_OK);
        assert(!strcmp(requested,"http://custom.test/retry-build"));
      }
    C
    File.write(temporary + '/test.c', code)
    out, err, status = Open3.capture3('cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', temporary + '/test.c', '-o', temporary + '/test')
    abort out + err unless status.success?
    out, err, status = Open3.capture3(temporary + '/test')
    abort out + err unless status.success?
  end
end
puts 'Guided source defaults honor architecture or flat release prefixes and preserve custom retry URLs'
