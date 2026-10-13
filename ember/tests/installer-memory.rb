# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual installer allocation and guided-language regression, prepared with AI assistance.
# Run the real language-selection ownership code with allocation failures.
require 'open3'
require 'tmpdir'
require 'fileutils'

root = File.expand_path(ARGV[0] || '../../usr.sbin/sysinst', __dir__)
main = File.read("#{root}/main.c")
language = main.split('struct found_msgs {', 2).last.split('#ifndef md_may_remove_boot_medium', 2).first
abort 'No language selection' unless language.include?('select_language(')
language = 'struct found_msgs {' + language

Dir.mktmpdir('ember-installer-memory') do |dir|
  cats = File.join(dir, 'catalogs')
  FileUtils.mkdir_p(cats)
  20.times { |i| File.write(File.join(cats, "sysinstmsgs.fixture#{i}"), 'fixture') }
  source = <<~C
    #include <assert.h>
    #include <dirent.h>
    #include <limits.h>
    #include <locale.h>
    #include <stdbool.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    static void *live[256];
    static int calls, fail_at, count;
    static void *allocate(size_t n) {
      if (++calls == fail_at) return NULL;
      void *p = malloc(n);
      assert(p != NULL);
      live[count++] = p;
      return p;
    }
    static void release(void *p) {
      if (p == NULL) return;
      int i;
      for (i = 0; i < count && live[i] != p; i++);
      assert(i < count);
      live[i] = live[--count];
      free(p);
    }
    static void *resize(void *p, size_t n) {
      if (++calls == fail_at) return NULL;
      int i;
      for (i = 0; i < count && live[i] != p; i++);
      assert(i < count);
      void *q = realloc(p, n);
      assert(q != NULL);
      live[i] = q;
      return q;
    }
    static char *duplicate(const char *s) {
      char *p = allocate(strlen(s) + 1);
      if (p) strcpy(p, s);
      return p;
    }
    static void *zero_allocate(size_t n, size_t size) {
      void *p = allocate(n * size);
      if (p) memset(p, 0, n * size);
      return p;
    }
    typedef struct { int cursel; } menudesc;
    typedef struct { const char *opt_name; int (*opt_action)(menudesc *, void *); } menu_ent;
    #define MSG_sysinst_message_language 1
    #define MSG_sysinst_message_locale 2
    #define MSG_hello 3
    #define MC_NOEXITOPT 0
    static bool foreign;
    static bool wizard_active;
    static bool cancel_language;
    static int wizard_initial_choice(const char *title, const char *const *items, int n, int selected) {
      (void)title; (void)selected;
      assert(n > 1 && items[0] && items[n - 1]);
      return cancel_language ? -1 : n - 1;
    }
    static const char *msg_string(int id) {
      return id == 1 ? (foreign ? "Fixture language" : "English") : NULL;
    }
    static int msg_file(const char *path) { foreign = path != NULL; return 0; }
    static int set_language(menudesc *m, void *arg) { (void)m; (void)arg; return 1; }
    static int new_menu(const char *s, menu_ent *e, int n, int a, int b, int c, int d, int f, ...) {
      (void)s; (void)e; (void)n; (void)a; (void)b; (void)c; (void)d; (void)f;
      return -1;
    }
    static void msg_display(int i) { (void)i; }
    static void process_menu(int i, void *p) { (void)i; (void)p; }
    static void free_menu(int i) { (void)i; }
    #define malloc allocate
    #define calloc zero_allocate
    #define realloc resize
    #define strdup duplicate
    #define free release
    #{language}
    int main(int argc, char **argv) {
      assert(argc == 2);
      for (int mode = 0; mode < 3; mode++) {
        wizard_active = mode != 0;
        cancel_language = mode == 2;
        for (fail_at = 0; fail_at <= 50; fail_at++) {
          calls = 0;
          foreign = false;
          select_language(argv[1]);
          assert(count == 0);
        }
      }
      return 0;
    }
  C
  path = File.join(dir, 'memory.c')
  binary = File.join(dir, 'memory')
  File.write(path, source)
  out, err, status = Open3.capture3(ENV.fetch('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
    '-Wno-unused-function', '-fsanitize=address,undefined', path, '-o', binary)
  abort out + err unless status.success?
  failures = []
  [File.join(dir, 'missing'), cats].each do |input|
    out, err, status = Open3.capture3(binary, input, chdir: dir)
    failures << out + err unless status.success?
  end
  net = File.read("#{root}/net.c")
  pm = main[/\tpm_new = malloc\(.*?\tmemset\(pm_new, 0, sizeof \*pm_new\);/m]
  network = net[/\tbuf = malloc .*?(?=\tfor \(i = 0, buf_tmp)/m]
  cleanup = main.split('/* Ensure we aren\'t inside the target tree */', 2).last.split('unwind_mounts();', 2).first
  abort 'Missing allocation or cleanup path' unless pm && network && cleanup
  { 'partition' => pm, 'network' => network, 'cleanup' => cleanup }.each do |name, body|
    code = <<~C
      #include <assert.h>
      #include <stdbool.h>
      #include <stdlib.h>
      #include <string.h>
      #include <stdarg.h>
      #define STRSIZE 1024
      static bool fail_memory;
      static void *allocate(size_t n) { return fail_memory ? NULL : malloc(n); }
      static void fatal(int status, const char *s, ...) {
        assert(fail_memory && status == EXIT_FAILURE && strcmp(s, "malloc") == 0);
        exit(0);
      }
      static int safe_chdir(const char *s) { assert(s && strcmp(s, "/") == 0); return 0; }
      static void warning(const char *s, ...) { (void)s; }
      #define malloc allocate
      #define err fatal
      #define chdir safe_chdir
      #define warn warning
      int main(int argc, char **argv) {
        (void)argv;
        fail_memory = argc > 1;
        unsetenv("HOME");
        struct pm_devs { char data[16]; } *pm_new = NULL;
        char *buf = NULL, *buf_in = strdup("wm0");
        #{body}
        assert(!fail_memory);
        free(pm_new);
        free(buf);
        free(buf_in);
        return 0;
      }
    C
    File.write(path, code)
    out, err, status = Open3.capture3(ENV.fetch('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
      '-Wno-unused-function', '-fsanitize=address,undefined', path, '-o', binary)
    abort out + err unless status.success?
    arguments = name == 'cleanup' ? [[]] : [[], ['fail']]
    arguments.each do |args|
      out, err, status = Open3.capture3(binary, *args)
      failures << "#{name}: #{out}#{err}" unless status.success?
    end
  end
  abort failures.join("\n") unless failures.empty?
end
puts 'PASS: real language selection frees all allocations with absent catalogs, growth and 50 allocation-failure positions'
puts 'PASS: allocation failures stop partition/network setup; cleanup does not depend on HOME'
