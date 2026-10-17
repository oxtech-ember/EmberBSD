# SPDX-License-Identifier: BSD-2-Clause
# Check the real exported declaration against the constructor definition.
# Use GCC16_CC=/absolute/native/gcc16 for the GCC16 diagnostic gate.
require 'tmpdir'
require 'open3'
require 'shellwords'
root = File.expand_path('../..', __dir__)
dir = root + '/sys/external/bsd/drm2/dist/drm/nouveau/dispnv50'
header = File.read(ARGV[0] || dir + '/wndw.h')
source = File.read(dir + '/nouveau_dispnv50_wndw.c')
decl = header[/int nv50_wndw_new_\(.*?;/m] or abort 'Missing exported declaration'
definition = source[/int\nnv50_wndw_new_\(.*?\)\n\{/m] or abort 'Missing constructor definition'
code = source[0...source.index('#include')] + <<~C
  #include <assert.h>
  #include <stdint.h>
  typedef uint32_t u32;
  struct nv50_wndw_func; struct drm_device; struct nv50_wndw;
  enum drm_plane_type { DRM_PLANE_TYPE_PRIMARY };
  enum nv50_disp_interlock_type { NV50_DISP_INTERLOCK_BASE = 3 };
  #{decl}
  #{definition}
    (void)func; (void)dev; (void)type; (void)name; (void)index;
    (void)format; (void)pwndw;
    assert(heads == 0x40);
    assert(interlock_type == NV50_DISP_INTERLOCK_BASE);
    assert(interlock_data == 0x800);
    return 0;
  }
  int main(void) {
    return nv50_wndw_new_(0, 0, DRM_PLANE_TYPE_PRIMARY, "base", 0,
      0, 0x40, NV50_DISP_INTERLOCK_BASE, 0x800, 0);
  }
C
Dir.mktmpdir('ember-nouveau-signature-') do |d|
  File.write(d + '/test.c', code)
  gcc = Shellwords.split(ENV.fetch('GCC16_CC') { abort 'Set GCC16_CC to a native GCC16 compiler' })
  version, err, st = Open3.capture3(*gcc, '-dumpfullversion')
  abort 'The diagnostic gate requires GCC16' unless st.success? && version.start_with?('16.')
  out, err, st = Open3.capture3(*gcc, '-O2', '-std=c17', '-Wall', '-Wextra', '-Werror',
    '-Wenum-int-mismatch', d + '/test.c', '-o', d + '/test-gcc')
  abort out + err unless st.success?
  out, err, st = Open3.capture3(d + '/test-gcc')
  abort out + err unless st.success?
  cc = Shellwords.split(ENV.fetch('HOST_CC', 'cc'))
  flags = Shellwords.split(ENV.fetch('HOST_CFLAGS', '-O2 -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined'))
  out, err, st = Open3.capture3(*cc, *flags, d + '/test.c', '-o', d + '/test')
  abort out + err unless st.success?
  out, err, st = Open3.capture3(d + '/test')
  abort out + err unless st.success?
end
puts 'PASS: Nouveau window declaration and argument order'
