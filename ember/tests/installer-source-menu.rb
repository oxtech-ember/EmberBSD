# Origin: EmberBSD generated source-menu execution regression, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'

# Execute the generated display action with the generated menu types and IDs.
# A hidden HTTPS default must not leave Enter on an unavailable source.
dir = File.expand_path(ARGV.fetch(0) { abort 'Usage: ruby ember/tests/installer-source-menu.rb sysinst-object-dir' })
header = File.read(File.join(dir, 'menu_defs.h'))
number = header[/^#define MENU_cdnsources\s+(\d+)/, 1] or abort 'No CDN menu'
generated = File.read(File.join(dir, 'menu_defs.c'))
function = generated[/static void menu_#{number}_postact\(menudesc \*menu, void \*arg\)\n\{.*?^\}/m] or abort 'No CDN display action'
root = File.expand_path('../..', __dir__)
retry_value = File.read("#{root}/usr.sbin/sysinst/defs.h")[/^#define\s+SET_RETRY\s+([^\n]+)/, 1] or abort 'No retry value'

Dir.mktmpdir('ember-source-menu') do |temp|
  source = <<~C
    #include <assert.h>
    #include <stdbool.h>
    #define __dead __attribute__((noreturn))
    #include "menu_defs.h"
    #include "msg_defs.h"
    #define SET_RETRY #{retry_value}
    #{function}
    int main(void) {
        menu_ent entries[] = { { .opt_name = MSG_https }, { .opt_name = MSG_http } };
        menudesc menu = { .numopts = 2, .opts = entries, .cursel = 0 };
        int status = 999;
        assert(setenv("FTPSSLNOVERIFY", "1", 1) == 0);
        menu_#{number}_postact(&menu, &status);
        assert(status == SET_RETRY);
        assert(entries[0].opt_flags & OPT_IGNORE);
        assert(menu.cursel == 1); /* Enter must select the visible HTTP source. */
        assert(unsetenv("FTPSSLNOVERIFY") == 0);
        menu.cursel = 0;
        menu_#{number}_postact(&menu, &status);
        assert(!(entries[0].opt_flags & OPT_IGNORE));
        assert(menu.cursel == 0);
        menu.cursel = menu.numopts; /* Keep the Back selection. */
        assert(setenv("FTPSSLNOVERIFY", "1", 1) == 0);
        menu_#{number}_postact(&menu, &status);
        assert(menu.cursel == menu.numopts);
        return 0;
    }
  C
  file = File.join(temp, 'menu.c')
  binary = File.join(temp, 'menu')
  File.write(file, source)
  out, err, status = Open3.capture3(ENV.fetch('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
    '-fsanitize=address,undefined', '-I', dir, file, '-o', binary)
  abort out + err unless status.success?
  out, err, status = Open3.capture3(binary)
  abort out + err unless status.success?
end
puts 'PASS: generated CDN display action selects an available source, restores HTTPS and preserves Back'
