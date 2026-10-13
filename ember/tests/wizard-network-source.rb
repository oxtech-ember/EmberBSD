# Origin: EmberBSD direct installer source regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
source = File.read(root + '/usr.sbin/sysinst/net.c')
functions = %w[url_encode url_proto make_url ftp_fetch wizard_network_source do_ftp_fetch].map do |name|
  source[/^(?:static )?(?:char \*|const char \*|void|int)\n#{name}\([^\{]*\)\n\{.*?^\}/m] or abort "Missing #{name}"
end
code = <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <stdarg.h>
  #include <string.h>
  #include <ctype.h>
  #include <unistd.h>
  #include "wizard_ui.h"
  #define STRSIZE 1024
  #define SSTRSIZE 128
  #define XFER_FTP 0
  #define XFER_HTTP 1
  #define XFER_HTTPS 2
  #define XFER_HOST(x) ((x)==XFER_FTP ? 0 : 1)
  #define SET_OK 0
  #define SET_RETRY 1
  #define RUN_DISPLAY 1
  #define RUN_PROGRESS 2
  #define RUN_XFER_DIR 4
  #define RUN_CHROOT 8
  #define RFC1738_SAFE_LESS_SHELL "-_.+!,"
  #define RFC1738_SAFE_LESS_SHELL_PLUS_SLASH "-_.+!,/"
  struct ftpinfo {char xfer_host[2][STRSIZE],dir[STRSIZE],user[SSTRSIZE],pass[STRSIZE],proxy[STRSIZE];unsigned int xfer;};
  static struct ftpinfo ftp;
  static bool network_up=true;
  static char set_dir_bin[STRSIZE],ftp_set_dir_bin[STRSIZE]="wrong/architecture/binary/sets";
  static char ext_dir_bin[STRSIZE],ext_dir_src[STRSIZE],request[4096];
  static const char *xfer_dir="/tmp",*dist_postfix=".tar.xz",*dist_tgz_postfix=".tgz";
  static int (*fetch_fn)(const char *);
  static int config_network(int flags) {(void)flags;return 1;}
  static const char *target_prefix(void) {return "/target";}
  static const char *target_expand(const char *path) {(void)path;assert(false);return "";}
  static void dup_file_into_target(const char *path) {(void)path;assert(false);}
  static bool set_is_source(const char *name) {(void)name;return false;}
  static bool use_tgz_for_set(const char *name) {(void)name;return false;}
  static const char *set_dir_for_set(const char *name) {(void)name;return set_dir_bin;}
  static int run_program(int flags,const char *format,...) {
    assert(flags==(RUN_DISPLAY|RUN_PROGRESS|RUN_XFER_DIR));
    va_list ap;va_start(ap,format);vsnprintf(request,sizeof(request),format,ap);va_end(ap);return 0;
  }
  static int do_ftp_fetch(const char *,bool,struct ftpinfo *,bool);
  #{functions.join("\n")}
  int main(void) {
    unsetenv("FTPSSLNOVERIFY");
    assert(wizard_network_source("https://example.test/build-abc/")==SET_OK);
    assert(fetch_fn("base")==SET_OK);
    if(strcmp(request,"/usr/bin/ftp -a https://example.test/build-abc/base.tar.xz")) {
      fprintf(stderr,"Wrong set download request: %s\\n",request);return 1;
    }
    assert(wizard_network_source("http://example.test/retry/build-xyz")==SET_OK);
    assert(fetch_fn("kern-GENERIC")==SET_OK);
    assert(!strcmp(request,"/usr/bin/ftp -a http://example.test/retry/build-xyz/kern-GENERIC.tar.xz"));
    assert(wizard_network_source("ftp://example.test/build")==SET_RETRY);
    puts("Direct HTTP(S) sources retain their directory for actual set downloads and retry");
  }
C
Dir.mktmpdir do |temporary|
  File.write(temporary + '/test.c', code)
  output, error, status = Open3.capture3('cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
    '-I' + root + '/usr.sbin/sysinst', temporary + '/test.c', root + '/usr.sbin/sysinst/wizard_ui.c', '-o', temporary + '/test')
  abort output + error unless status.success?
  output, error, status = Open3.capture3(temporary + '/test')
  abort output + error unless status.success?
  puts output
end
