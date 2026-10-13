# SPDX-License-Identifier: BSD-2-Clause
# Execute the installer policy and set-directory call from the real sources.
require 'open3'
require 'tmpdir'

root = File.expand_path('../..', __dir__)
main = File.read("#{root}/usr.sbin/sysinst/main.c")
policy = main.split('partitions_init();', 2).last.split('/* Prevent kernel conole', 2).first
net = File.read("#{root}/usr.sbin/sysinst/net.c")
fetch = net.split(/^do_ftp_fetch\(/, 2).last
directory_call = fetch[/\bmake_url\(url, f,.*?;/m] or abort 'No OS set directory call'

Dir.mktmpdir('ember-https') do |dir|
  cases = {
    'trust' => <<~C,
      #include <assert.h>
      #include <stdbool.h>
      #include <stdlib.h>
      static bool no_openssl_trust_anchors_available(void) { return true; }
      static void configure(void) { #{policy} }
      int main(void) {
        unsetenv("FTPSSLNOVERIFY");
        configure();
        assert(getenv("FTPSSLNOVERIFY") == NULL);
        setenv("FTPSSLNOVERIFY", "1", 1);
        configure();
        assert(getenv("FTPSSLNOVERIFY") == NULL);
        return 0;
      }
    C
    'directories' => <<~C
      #include <assert.h>
      #include <stdbool.h>
      #include <string.h>
      struct ftpinfo { int unused; };
      static struct ftpinfo ftp, pkgsrc;
      static char set_dir_bin[] = "/amd64/binary/sets";
      static char ftp_set_dir_bin[] = "";
      static char set_dir_src[] = "/source/sets";
      static const char *selected;
      static bool set_is_source(const char *name) { return strcmp(name, "src") == 0; }
      static const char *set_dir_for_set(const char *name) {
        return set_is_source(name) ? set_dir_src : set_dir_bin;
      }
      static void make_url(char *url, struct ftpinfo *f, const char *dir) {
        (void)url; (void)f; selected = dir;
      }
      static void select_dir(struct ftpinfo *f, const char *set_name) {
        char url[1024];
        #{directory_call}
      }
      int main(void) {
        select_dir(&ftp, "base");
        assert(strcmp(selected, "") == 0);
        assert(strcmp(set_dir_bin, "/amd64/binary/sets") == 0);
        select_dir(&ftp, "src");
        assert(strcmp(selected, "/source/sets") == 0);
        select_dir(&pkgsrc, "pkgsrc");
        assert(strcmp(selected, "/amd64/binary/sets") == 0);
        return 0;
      }
    C
  }
  failures = []
  cases.each do |name, source|
    path = File.join(dir, "#{name}.c")
    binary = File.join(dir, name)
    File.write(path, source)
    out, err, status = Open3.capture3(ENV.fetch('CC', 'cc'), '-std=gnu11', '-Wall',
      '-Wno-unused-function', '-Wno-unused-variable', '-fsanitize=address,undefined', path, '-o', binary)
    abort out + err unless status.success?
    out, err, status = Open3.capture3(binary)
    failures << "#{name}: #{out}#{err}" unless status.success?
  end
  abort failures.join("\n") unless failures.empty?
end
puts 'PASS: HTTPS stays verified without anchors; remote binary paths do not change local or package paths'
