# Origin: Exercise the real temporary console policy through ioctl boundaries, 2026-10-13.
# SPDX-License-Identifier: BSD-2-Clause
require 'open3'
require 'tmpdir'
root = File.expand_path('../..', __dir__)
source = File.read(root + '/usr.sbin/sysinst/wizard_console.c')
source = source.sub(/.*?static bool palette_changed/m, 'static bool palette_changed')
code = <<~C
  #include <assert.h>
  #include <stdbool.h>
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <unistd.h>
  #include "wizard_ui.h"
  #define WSKBD 1
  enum {WSDISPLAYIO_GTYPE, WSDISPLAYIO_GINFO, WSDISPLAYIO_GFONT,
    WSDISPLAYIO_SFONT, WSDISPLAYIO_GETCMAP, WSDISPLAYIO_PUTCMAP,
    WSDISPLAYIO_GETPARAM};
  #define WSDISPLAY_TYPE_GENFB 8
  #define WSDISPLAY_TYPE_PCIMISC 9
  #define WSDISPLAYIO_PARAM_RGB_PALETTE 4
  struct wsdisplay_param {int param,min,max,curval;};
  struct wsdisplay_cmap {unsigned index,count; unsigned char *red,*green,*blue;};
  struct wsdisplay_usefontdata {char *name;};
  struct wsdisplay_fbinfo {unsigned width,height;};
  struct wsdisplay_getfont {char *gf_name;unsigned gf_size,gf_actual;};
  static unsigned type=8,width=1024,height=768;
  static int fail_get,fail_put,puts_count,font_sets;
  static char active_font[128]="Bold 8x16";
  static unsigned char active[16][3];
  static void wizard_console_restore(void);
  static int policy_ioctl(int fd,int op,void *data) {
    assert(fd==STDIN_FILENO);
    if(op==WSDISPLAYIO_GTYPE) *(unsigned *)data=type;
    else if(op==WSDISPLAYIO_GETPARAM) {
      struct wsdisplay_param *p=data;assert(p->param==WSDISPLAYIO_PARAM_RGB_PALETTE);
      p->min=0;p->max=1;p->curval=type==8 || type==9;
    }
    else if(op==WSDISPLAYIO_GINFO) {
      struct wsdisplay_fbinfo *p=data;p->width=width;p->height=height;
    } else if(op==WSDISPLAYIO_GFONT) {
      struct wsdisplay_getfont *p=data;strlcpy(p->gf_name,active_font,p->gf_size);
      p->gf_actual=strlen(active_font)+1;
    } else if(op==WSDISPLAYIO_SFONT) {
      struct wsdisplay_usefontdata *p=data;strlcpy(active_font,p->name,sizeof(active_font));font_sets++;
    } else {
      struct wsdisplay_cmap *p=data;assert(p->index==0 && p->count==16);
      if(op==WSDISPLAYIO_GETCMAP && fail_get) return -1;
      if(op==WSDISPLAYIO_PUTCMAP && fail_put) return -1;
      for(int i=0;i<16;i++) {
        if(op==WSDISPLAYIO_GETCMAP) {p->red[i]=active[i][0];p->green[i]=active[i][1];p->blue[i]=active[i][2];}
        else {active[i][0]=p->red[i];active[i][1]=p->green[i];active[i][2]=p->blue[i];}
      }
      if(op==WSDISPLAYIO_PUTCMAP) puts_count++;
    }
    return 0;
  }
  #define ioctl policy_ioctl
  #{source.sub('void\nwizard_console_restore', 'static void\nwizard_console_restore')}
  int main(void) {
    memset(active,0x7f,sizeof(active));
    type=99;assert(!wizard_console_setup());assert(font_sets==0 && puts_count==0);
    type=9;assert(wizard_console_setup());assert(strcmp(active_font,"Spleen 12x24")==0);
    assert(memcmp(active,wizard_console_rgb,sizeof(active))==0);
    assert(wizard_console_setup());assert(font_sets==1 && puts_count==1);
    wizard_console_restore();assert(strcmp(active_font,"Bold 8x16")==0);
    for(size_t i=0;i<sizeof(active);i++) assert(((unsigned char *)active)[i]==0x7f);
    assert(font_sets==2 && puts_count==2);wizard_console_restore();assert(puts_count==2);
    width=1280;height=800;fail_put=1;assert(!wizard_console_setup());
    assert(strcmp(active_font,"Spleen 16x32")==0);fail_put=0;wizard_console_restore();
    assert(strcmp(active_font,"Bold 8x16")==0 && puts_count==2);
    fail_get=1;assert(!wizard_console_setup());wizard_console_restore();assert(puts_count==2);
    return 0;
  }
C
Dir.mktmpdir do |dir|
  File.write(dir + '/policy.c', code)
  out, err, status = Open3.capture3('cc', '-fsanitize=address,undefined', '-I' + root + '/usr.sbin/sysinst',
    dir + '/policy.c', root + '/usr.sbin/sysinst/wizard_ui.c', '-o', dir + '/policy')
  abort out + err unless status.success?
  out, err, status = Open3.capture3(dir + '/policy')
  abort out + err unless status.success?
end
puts 'PASS: real console policy saves/restores font and palette; failures and unsupported drivers do not overwrite colors'
