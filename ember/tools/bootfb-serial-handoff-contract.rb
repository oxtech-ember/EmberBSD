#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Compose the actual close and PLCOM fixtures. No copied driver implementation.
require 'tmpdir'
require 'fileutils'
require 'open3'
require 'shellwords'
require 'digest'
require 'json'
require 'timeout'
require 'rbconfig'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['SERIAL_HANDOFF_EVIDENCE']
abort 'new evidence required' if evidence && File.exist?(evidence)
FileUtils.mkdir_p(evidence) if evidence
commands = []
last_err = ''
run = lambda do |name, argv, env = {}|
  out = err = ''; status = nil
  Open3.popen3(env, *argv, pgroup: true) do |i,o,e,w|
    i.close; a = Thread.new { o.read }; b = Thread.new { e.read }
    begin
      Timeout.timeout(45) { status = w.value }
    rescue Timeout::Error
      Process.kill('KILL', -w.pid); status = w.value
    end
    out = a.value; err = b.value
  end
  commands << {name:name, argv:argv, env:env, status:status.exitstatus, signal:status.termsig}
  if evidence
    File.write(evidence+'/'+name+'.stdout',out);File.write(evidence+'/'+name+'.stderr',err)
    File.write(evidence+'/commands.json',JSON.pretty_generate(commands)+"\n")
  end
  print out; warn err unless err.empty?; last_err = err; status
end
def function(text,name)
  a=text.scan(/^(?:static )?(?:void|int|bool)\n#{Regexp.escape(name)}\(.*?^}$/m)
  abort "function count #{name}: #{a.size}" unless a.size==1
  a.first
end
def notice(text)
  text.split(/^#(?:include|ifdef|ifndef)/,2).first
end
paths=%w[sys/kern/subr_autoconf.c sys/arch/arm/broadcom/bcm2712_v3d_acpi.c sys/dev/cons.c
  ember/tools/bootfb-serial-handoff-contract.c ember/tools/bootfb-serial-handoff-contract.rb
  sys/arch/arm/broadcom/bcm2712_display_handoff.c
  sys/arch/arm/broadcom/bcm2712_display_handoffvar.h
  sys/dev/wscons/wsdisplay_bootfb.c sys/dev/wscons/wsdisplay_bootfbvar.h
  sys/arch/arm/broadcom/bcm2835_mbox.c sys/arch/arm/broadcom/bcm2835_mbox.h sys/sys/endian.h]
files=paths.to_h { |p| [p,File.binread(source+'/'+p)] }
Dir.mktmpdir('bootfb-serial-handoff-',ENV.fetch('TMPDIR','/tmp')) do |work|
  puts 'native serial handoff temporary directory: '+work
  begin
    s=run.call('export-close',[RbConfig.ruby,source+'/ember/tools/console-close-contract.rb',source],
      {'SKIP_MUTANTS'=>'1','COMPILE_ONLY'=>'1','CONSOLE_CLOSE_EVIDENCE'=>work+'/close'})
    abort 'close export failed' unless s.success?
    s=run.call('export-ready',[RbConfig.ruby,source+'/ember/tools/serial-console-ready-contract.rb',source],
      {'SKIP_MUTANTS'=>'1','COMPILE_ONLY'=>'1','SERIAL_READY_EVIDENCE'=>work+'/ready'})
    abort 'provider export failed' unless s.success?
    s=run.call('export-mmu4',[RbConfig.ruby,source+'/ember/tools/bcmv3d-mmu4-native-contract.rb',source],
      {'MMU4_NATIVE_COMPILE_ONLY'=>'1','MMU4_NATIVE_EVIDENCE'=>work+'/mmu4'})
    abort 'MMU4 export failed' unless s.success?
    native_ref=File.read(source+'/ember/tools/bcmv3d-mmu4-native-contract.c')
    abort 'native main changed' unless native_ref.scan(/main\(void\)/).size == 1
    files['ember/tools/bcmv3d-mmu4-native-contract.c']=native_ref
    File.write(work+'/mmu4/native-reference.h',native_ref.sub('main(void)','reference_native_mmu4_main(void)'))
    %w[core-types.h native-types.h mmu4-types.h backing-types.h adapter-types.h].each do |n|
      FileUtils.cp(work+'/mmu4/'+n,work+'/mmu4-'+n)
    end
    FileUtils.cp_r(work+'/close/inputs/.',work)
    FileUtils.cp_r(work+'/ready/actual-inputs/.',work)
    auto=files.fetch(paths[0]); acpi=files.fetch(paths[1])
    state=auto[/^struct deferred_config \{.*?^TAILQ_HEAD\(deferred_config_head, deferred_config\);/m]
    queue=auto[/^static struct deferred_config_head interrupt_config_queue =\n\s*TAILQ_HEAD_INITIALIZER\(interrupt_config_queue\);/]
    final=auto[/^struct finalize_hook \{.*?^static int config_finalize_done;/m]
    abort 'config state absent' unless state && queue && final
    glue=<<~C
      static int config_misc_lock, config_misc_cv, boothowto, config_do_twiddle;
      static unsigned hz=100;
      static bool init_started;
      static TAILQ_HEAD(, device) config_pending = TAILQ_HEAD_INITIALIZER(config_pending);
      #define KM_SLEEP 1
      #define AB_QUIET 1
      #define AB_SILENT 2
      #define AB_VERBOSE 4
      #define printf_nolog printf
      static void native_message(const char *f, ...) { (void)f; }
      #define aprint_debug native_message
      #define aprint_normal native_message
      #define aprint_debug_dev(...) ((void)0)
      #ifndef __printflike
      #define __printflike(a,b)
      #endif
      static const char *device_xname(device_t d) { (void)d; return "fixture"; }
      static void mutex_enter(int *p) { (void)p; }
      static void mutex_exit(int *p) { (void)p; }
      static void *kmem_alloc(size_t n, int f) { CHECK(f==KM_SLEEP); void *p=malloc(n); CHECK(p!=NULL); return p; }
      static void kmem_free(void *p,size_t n) { CHECK(n>0); free(p); }
      static unsigned getticks(void) { return 0; }
      static unsigned mstohz(unsigned n) { return n; }
      static int aprint_get_error_count(void) { return 0; }
      static void cv_broadcast(int *p) { CHECK(p==&config_misc_cv); }
      static bool device_pmf_is_registered(device_t d) { CHECK(d!=NULL); return true; }
      static void kthread_exit(int n) { CHECK(n==0); }
      static void config_interrupts_thread(void *);
      static unsigned joined;
      static int cv_timedwait(int *c,int *m,unsigned n) {
        CHECK(c==&config_misc_cv && m==&config_misc_lock && n==1000 && !init_started);
        config_interrupts_thread(NULL); joined++; return 0;
      }
      struct pdevinit { void (*pdev_attach)(int); int pdev_count; };
      static struct pdevinit pdevinit[]={{NULL,0}};
    C
    body=%w[config_pending_incr config_pending_decr config_interrupts config_interrupts_thread config_finalize_register config_finalize].map { |n| function(auto,n) }.join("\n")
    body.sub!('config_interrupts_thread(void *cookie)', 'config_interrupts_thread(void *cookie __attribute__((__unused__)))')
    File.write(work+'/native-config.h',notice(auto)+glue+state+"\n"+queue+"\n"+final+"\n"+body)
    owner=acpi[/^enum bcmv3d_clock_status \{.*?(?=^static int bcmv3d_match)/m]
    abort 'owner missing' unless owner
    File.write(work+'/native-owner.h',notice(acpi)+owner)
    %w[bcm2712_display.h bcm2712_display_clockvar.h bcm2712_display_hsm.h bcm2712_display_acpi.h bcm2712_display_bvb.h bcm2712_display_core.h bcm2712_display_disp.h].zip(%w[display-types.h clockvar.h hsm-types.h acpi-types.h bvb-types.h core-types.h disp-types.h]).each do |input,output|
      p='sys/arch/arm/broadcom/'+input; t=File.read(source+'/'+p); files[p]=t
      File.write(work+'/'+output,t.gsub(/^#include .*\n/,''))
    end
    %w[bcm2712_display_clock.c bcm2712_display_clock_acpi.c].zip(%w[clock-core.h native-clock.h]).each do |input,output|
      p='sys/arch/arm/broadcom/'+input; t=File.read(source+'/'+p); files[p]=t
      File.write(work+'/'+output,t.gsub(/^#include .*\n|^__KERNEL_RCSID\(.*\n/,''))
    end
    drv='sys/external/bsd/drm2/bcmv3d/'
    arm='sys/arch/arm/broadcom/'
    {'range-check-types.h'=>drv+'bcmv3d_mmu4_check.h',
     'publish-types.h'=>drv+'bcmv3d_hvs_publish.h',
     'publish-body.h'=>drv+'bcmv3d_hvs_publish.c',
     'publish-hvs-types.h'=>arm+'bcm2712_display_hvsvar.h',
     'publish-hvs-body.h'=>arm+'bcm2712_display_hvs.c',
     'publish-pv-types.h'=>arm+'bcm2712_display_pv0var.h',
     'publish-pv-body.h'=>arm+'bcm2712_display_pv0.c',
     'publish-hdmi-types.h'=>arm+'bcm2712_display_hdmi0var.h',
     'publish-hdmi-body.h'=>arm+'bcm2712_display_hdmi0.c'}.each do |name,path|
      t=File.binread(source+'/'+path);files[path]=t
      File.write(work+'/'+name,t.gsub(/^#include .*\n|^__KERNEL_RCSID\(.*\n/,''))
    end
    # Reuse the published independent bus oracle. Only fixture wiring changes.
    p='ember/tools/bcmv3d-hvs-publish-contract.c';t=File.binread(source+'/'+p);files[p]=t
    oracle=t[/^struct profile_case \{.*?(?=^static void\nnative_gate)/m] or abort 'publish oracle absent'
    File.write(work+'/mmu4/publish-native-gates.h',notice(t)+function(t,'native_gate'))
    oracle.sub!('CHECK(!owner->lock.held && !bcm2712_iommuc_mutex.held);','bootfb_publish_locks();')
    oracle.gsub!('bcmv3d_hvs_publish_1080p60(owner,','bcmv3d_hvs_publish_1080p60(gpu_sc.sc_bootfb.mmu4.native,')
    oracle.gsub!('pb.status','gpu_sc.sc_bootfb.publish.status')
    oracle.sub!('value = pb.eof ? 0x006000 : 0x004000;', 'value = (pb.eof ? 0x006000 : 0x004000) | (hvs_progress_delayed ? 0x10000 : 0);')
    File.write(work+'/publish-oracle.h',notice(t)+oracle)
    %w[bcm2712_display_quiesce.h bcm2712_display_quiesce.c].each do |n|
      p='sys/arch/arm/broadcom/'+n; t=File.read(source+'/'+p);files[p]=t;File.write(work+'/'+n,t)
    end
    p='sys/arch/arm/broadcom/bcm2712_display.c';t=File.read(source+'/'+p);files[p]=t
    resources=t[/^#define MEM\(.*?^#undef MEM/m] or abort 'resource table absent'
    File.write(work+'/native-resources.h',notice(t)+resources)
    t=File.read(source+'/ember/tools/bcm2712-display-clock-contract.c')
    files['ember/tools/bcm2712-display-clock-contract.c']=t
    File.write(work+'/clock-firmware.h',t[/^#define LOG_MAX .*?(?=^static void)/m] || abort('clock service absent'))
    p='sys/arch/evbarm/rpi/vcprop.h';t=File.read(source+'/'+p);files[p]=t
    File.write(work+'/properties.h',t.lines.grep(/^#define\s+(?:VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_DISP|VCPROP_CLK_CORE|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MIN_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)\s/).join)

    File.write(work+'/bcm2712_display_handoffvar.h',files.fetch('sys/arch/arm/broadcom/bcm2712_display_handoffvar.h'))
    File.write(work+'/protocol.c',files.fetch('sys/arch/arm/broadcom/bcm2712_display_handoff.c'))
    endian=files.fetch('sys/sys/endian.h')
    endian_functions=%w[le16dec le32dec].map do |n|
      matches=endian.scan(/^static __inline uint(?:16|32)_t __unused\n#{n}\(.*?^}/m)
      abort 'endian function missing '+n unless matches.size==1
      matches.first
    end
    File.write(work+'/native-endian.h',notice(endian)+"\n#ifndef __CAST\n#define __CAST(t,v) ((t)(v))\n#endif\n#ifndef __unused\n#define __unused __attribute__((__unused__))\n#endif\n"+endian_functions.join("\n"))
    helpers=%w[bcmv3d_bootfb_exchange bcmv3d_bootfb_notify bcmv3d_display_read bcmv3d_display_write bcmv3d_display_barrier bcmv3d_display_delay bcmv3d_display_clocks bcmv3d_bootfb_stop_display bcmv3d_bootfb_writeback_idle bcmv3d_bootfb_program_mmu4 bcmv3d_publish_io_error bcmv3d_publish_read bcmv3d_publish_write bcmv3d_publish_barrier bcmv3d_publish_delay bcmv3d_bootfb_publish bcmv3d_bootfb_detect bcmv3d_bootfb_start_vblank bcmv3d_bootfb_claim_job bcmv3d_bootfb_queue bcmv3d_bootfb_retain bcmv3d_bootfb_cancel bcmv3d_finalize]
    File.write(work+'/native-job.h',notice(acpi)+(acpi[/^static const unsigned int bcmv3d_display_indices.*?^};/m] || abort("indices absent"))+"\n"+(acpi[/^static const unsigned int bcmv3d_publish_indices.*?^};/m] || abort('publish indices absent'))+"\nstatic void bcmv3d_bootfb_retain(struct bcmv3d_softc *, int);\nstatic void bcmv3d_bootfb_cancel(struct bcmv3d_softc *, int);\n"+helpers.map { |n| function(acpi,n) }.join("\n"))
    ws=File.read(source+'/sys/dev/wscons/wsdisplay.c')
    commit=ws[/^\t\twsdisplay_cons.cn_dev = makedev\(maj,\n\t\t    WSDISPLAYMINOR\(device_unit\(self\), 0\)\);\n\t\twsdisplay_console_generation\+\+;/]
    abort 'actual cdev commit missing' unless commit
    File.write(work+'/native-cdev-commit.h',notice(ws)+"static void fixture_cdev_commit(device_t self, int maj)\n{\n"+commit+"\n}\n")
    File.write(work+'/native-cnopen.h',notice(files.fetch(paths[2]))+function(files.fetch(paths[2]),'cnopen').sub('int mode, struct lwp *l)', 'int mode __attribute__((__unused__)), struct lwp *l __attribute__((__unused__)))'))
    # The real ACPI attachment must select the activate callback only when
    # bootfb handoff is enabled. Keep both complete declarations from source.
    front=File.read(source+'/sys/arch/arm/acpi/plcom_acpi.c')
    declaration=front[/^#ifdef WSDISPLAY_BOOTFB_HANDOFF\nCFATTACH_DECL_NEW\(plcom_acpi,.*?^#endif/m]
    abort 'conditional provider declarations missing' unless declaration
    abort 'conditional provider declaration count changed' unless declaration.scan(/^CFATTACH_DECL_NEW\(plcom_acpi,/).size==2
    %w[bootfb-display-services.h bcm2712-quiesce-fixture.h bootfb-mmu4-services.c bootfb-mmu4-backing-services.c bootfb-hvs-services.h].each { |n| files['ember/tools/'+n]=File.binread(source+'/ember/tools/'+n) }
    cc=Shellwords.split(ENV.fetch('CC','cc'));flags=Shellwords.split(ENV.fetch('CFLAGS',''))
    File.write(work+'/provider-profile.c', <<~C + declaration + "\n" + <<~C)
      #include <stddef.h>
      struct cfattach { int (*activate)(void *, int); };
      int plcom_activate(void *d, int a) { (void)d; return a; }
      #define CFATTACH_DECL_NEW(n,s,m,a,d,v) struct cfattach n ## _ca = { v }
    C
      int main(void) {
      #ifdef WSDISPLAY_BOOTFB_HANDOFF
        return plcom_acpi_ca.activate == plcom_activate ? 0 : 1;
      #else
        return plcom_acpi_ca.activate == NULL ? 0 : 1;
      #endif
      }
    C
    runner=ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
    [false,true].each do |on|
      argv=[*cc,'-std=c11','-Wall','-Wextra','-Werror',*flags,*(on ? ['-DWSDISPLAY_BOOTFB_HANDOFF'] : []),work+'/provider-profile.c','-o',work+'/profile']
      abort 'provider conditional ABI compile failed' unless run.call('profile-compile-'+on.to_s,argv).success?
      abort 'provider conditional ABI failed' unless ENV['COMPILE_ONLY']=='1' || run.call('profile-'+on.to_s,[*runner,work+'/profile']).success?
    end
    runner=ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
    mmu_common=[*cc,'-std=c11','-Wall','-Wextra','-Werror',*flags]
    mmu_objects=[]
    back=work+'/mmu4/icac/backing'
    %w[bcmv3d_mmu4_backing.c bcmv3d_hvs_prepare.c arm/broadcom/bcm2712_mmu4_table.c].each do |n|
      out=work+'/'+File.basename(n)+'.o'
      abort 'MMU4 backing compile failed' unless run.call('mmu4-'+File.basename(n),mmu_common+['-I'+back,'-c',back+'/'+n,'-o',out]).success?
      mmu_objects << out
    end
    %w[bootfb-mmu4-services.c bootfb-mmu4-backing-services.c].each do |n|
      out=work+'/'+n+'.o'
      inc=n.include?('backing') ? back : work+'/mmu4'
      argv=mmu_common+['-I'+inc,'-I'+source+'/sys/external/bsd/acpica/dist/include','-I'+source+'/ember/tools','-c',source+'/ember/tools/'+n,'-o',out]
      abort 'MMU4 service compile failed' unless run.call('mmu4-'+n,argv).success?
      mmu_objects << out
    end
    compile=lambda do |name|
      common=[*cc,'-std=c11','-Wall','-Wextra','-Werror','-pthread',*flags,'-I'+work,'-I'+source+'/ember/tools']
      s=run.call('compile-provider-'+name,[*common,'-DSERIAL_READY_LIBRARY','-Dmain=serial_ready_original_main','-c',source+'/ember/tools/serial-console-ready-contract.c','-o',work+'/provider.o'])
      abort 'provider compile failed' unless s.success?
      s=run.call('compile-route-'+name,[*common,'-DBCM2712_V3D_RENDER','-DBCM2712_V3D_TAKEOVER',source+'/ember/tools/bootfb-serial-handoff-contract.c',work+'/protocol.c',work+'/bcm2712_display_quiesce.c',work+'/provider.o',*mmu_objects,'-o',work+'/contract'])
      abort 'route compile failed' unless s.success?
    end
    compile.call('positive')
    unless ENV['COMPILE_ONLY']=='1'
      abort 'native handoff positive failed' unless run.call('positive',[*runner,work+'/contract']).success?
    end
controls={
"kms-detect-clock"=>["native-job.h", "d->hsm_status.leases != 1 ||", "false ||", "bcmv3d_bootfb_detect(&gpu_sc, &connected) == EPERM"],
"kms-detect-map"=>["native-job.h", "(p->mapped & (1U << BCMV3D_PUBLISH_HDMI0)) == 0 ||", "false ||", "bcmv3d_bootfb_detect(&gpu_sc, &connected) == EPERM"],
"kms-detect-bounds"=>["native-job.h", "d->contract.memory[16].length < 0x1cc", "false", "bcmv3d_bootfb_detect(&gpu_sc, &connected) == EPERM"],
"kms-detect-firmware"=>["native-job.h", "!owner->firmware.complete ||", "false ||", "bcmv3d_bootfb_detect(&gpu_sc, &connected) == EPERM"],
"kms-detect-bit"=>["native-job.h", "(value & 1U) != 0", "value != 0", "connected == ((raw & 1) != 0)"],
"kms-detect-io-error"=>["native-job.h", "return EIO;", "return 0;", "bcmv3d_bootfb_detect(&gpu_sc, &connected) == EIO && connected"],
"kms-bind-call"=>["native-job.h", "error = bcmv3d_kms_bind(owner->renderer, bcmv3d_bootfb_detect, sc);", "error = ((void)bcmv3d_kms_bind, 0);", "kms_bind_calls == 1 && kms_hpd_reads == 1"],
'hvs-owner-attempt'=>['native-job.h','p->attempted = p->executing = true;','p->executing = true;','gpu_sc.sc_bootfb.publish.attempted'],
'hvs-owner-range-call'=>['native-job.h',"if (error == 0)\n\t\tcheck_error = bcmv3d_mmu4_check_ranges", "if (error != 0)\n\t\tcheck_error = bcmv3d_mmu4_check_ranges",'gpu_sc.sc_bootfb.publish.status.complete'],
'hvs-owner-range-lifetime'=>['native-job.h',"if (error == 0)\n\t\tcheck_error = bcmv3d_mmu4_check_ranges", "if (error == 0 && (p->executing = true))\n\t\tcheck_error = bcmv3d_mmu4_check_ranges",'!gpu_sc.sc_bootfb.publish.executing'],
'hvs-owner-range-result'=>['native-job.h','p->fault = error;','p->fault = error != 0 ? error : check_error;','gpu_sc.sc_bootfb.publish.fault == 0'],
'hvs-owner-giant'=>['native-job.h','KERNEL_UNLOCK_ALL(NULL, &count);','count = 1; KERNEL_UNLOCK_ONE(NULL);','hvs_permitted && giant == 0'],
'hvs-owner-repeat'=>['native-job.h','if (p->attempted || p->complete || p->fault != 0)','if (false)','bcmv3d_bootfb_publish(&gpu_sc) == EBUSY && giant == 0'],
'hvs-owner-mmu4'=>['native-job.h','!owner->mmu4.complete ||','false ||','hvs_permitted'],
'hvs-owner-hold'=>['native-job.h','error = wsdisplay_bootfb_serial_closed(owner->lease);','error = 0;','hvs_permitted'],
'hvs-owner-close'=>['native-job.h','error = simplefb_console_close_status(owner->lease, &close);','error = 0; memset(&close,1,sizeof(close)); close.error = close.mi_error = close.md_error = 0;','hvs_permitted'],
'hvs-owner-clock'=>['native-job.h','HELD(disp);','/* skip DISP lifetime */','hvs_permitted'],
'hvs-owner-pipe1'=>['native-job.h','for (i = 0; i < 2; i++) {','for (i = 0; i < 1; i++) {','hvs_permitted'],
'hvs-owner-first'=>['native-job.h','error != 0 && owner->first_error == 0','error != 0','gpu_sc.sc_bootfb.first_error == EPROTO'],
'hvs-owner-upm'=>['native-job.h','.upm_offset = 0,','.upm_offset = 256,','value == plane_word(pb.dlist_writes)'],
'hvs-read-busy'=>['native-job.h','(p->writeback[i] & 3U) != 0','false','p->fault == (mode < 2 ? EBUSY : EIO)'],
'hvs-read-cxm'=>['native-job.h','p->cxm_words < p->dlist_first + p->dlist_words','p->cxm_words < p->dlist_first','words >= 73 && variant == (uint32_t)p->frame.variant && out != NULL'],
'hvs-read-align'=>['native-job.h','(offset & 3U) != 0 || memory->length < 4','false || memory->length < 4','hvs_callback_case == 0'],
'hvs-read-sticky'=>['native-job.h','if (p->io_error != 0)','if (false)','hvs_callback_case == 0'],
'hvs-read-executing'=>['native-job.h','if (!p->executing)','if (false)','bcmv3d_publish_read(&gpu_sc,BCMV3D_PUBLISH_HVS,0,&value) == EPERM'],
'hvs-write-ro'=>['native-job.h','if (window != BCMV3D_PUBLISH_HVS && window != BCMV3D_PUBLISH_PV0)','if (false)','error == (hvs_callback_case == 6 ? EACCES : EINVAL)'],
'hvs-index'=>['native-job.h','3, 5, 6, 11, 16, 17, 18, 19, 22','3, 5, 6, 11, 17, 17, 18, 19, 22','i == (unsigned)gpu_sc.sc_bootfb.publish.status.window'],
'hvs-helper-native'=>['publish-body.h','error = bcmv3d_mmu4_native_cached_status(native, &s->native);','error = 0;','p->attempted && p->complete'],
'hvs-helper-version'=>['publish-body.h','READ(BCMV3D_PUBLISH_HVS, 0);','READ(BCMV3D_PUBLISH_HVS, 0x20);','window == 0 && offset == 0'],
'hvs-helper-lptr'=>['publish-body.h','WRITE(BCMV3D_PUBLISH_HVS, lptr, PUBLISH_HEAD);','WRITE(BCMV3D_PUBLISH_HVS, lptr, 128);','pb.barriers == 10 && value == 32'],
'hvs-owner-native'=>['native-job.h','bcmv3d_hvs_publish_1080p60(owner->mmu4.native,','bcmv3d_hvs_publish_1080p60(NULL,','p->attempted && p->complete'],
'hvs-link-log'=>['native-job.h','p->status.link[19]','p->status.link[18]','strcmp(actual,expected) == 0'],
'hvs-csc-log'=>['native-job.h','p->status.link[25]','p->status.link[24]','strcmp(actual,expected) == 0'],
'hvs-timing-log'=>['native-job.h','p->status.timing.pv[2],','p->status.timing.pv[3],','strcmp(actual,expected) == 0'],


  'mmu4-context'=>['native-job.h','if (cold || panicstr != NULL || cpu_intr_p() || cpu_softintr_p())','if (false)','bcmv3d_bootfb_program_mmu4(&gpu_sc) == EWOULDBLOCK'],
  'mmu4-closed'=>['native-job.h','owner->phase != BCMV3D_BOOTFB_CLOSED ||','false ||','mmu4_permitted'],
  'mmu4-fw'=>['native-job.h','!owner->firmware.complete ||','false ||','mmu4_permitted'],
  'mmu4-stop'=>['native-job.h','!d->complete ||','false ||','mmu4_permitted'],
  'mmu4-pipe1'=>['native-job.h','for (i = 0; i < 2; i++) {','for (i = 0; i < 1; i++) {','mmu4_permitted'],
  'mmu4-clock'=>['native-job.h','HELD(disp);','/* no DISP lease check */','mmu4_permitted'],
  'mmu4-held'=>['native-job.h','error = wsdisplay_bootfb_serial_closed(owner->lease);','error = 0;','mmu4_permitted'],
  'mmu4-lease'=>['native-job.h','error = simplefb_console_close_status(owner->lease, &close);','error = 0; memset(&close,1,sizeof(close)); close.error = close.mi_error = close.md_error = 0;','mmu4_permitted'],
  'mmu4-tuple'=>['native-job.h',"error = bcmv3d_render_bootfb_validate(owner->renderer, sc->sc_dev,\n\t\t    sc->sc_bst, sc->sc_dmat, sc->sc_handle, owner);",'error = ((void)bcmv3d_render_bootfb_validate, 0);','mmu4_permitted'],
  'mmu4-attempt'=>['native-job.h','m->attempted = true;','/* no publication */','gpu_sc.sc_bootfb.mmu4.attempted'],
  'mmu4-giant'=>['native-job.h','KERNEL_UNLOCK_ALL(NULL, &count);','count = 1; KERNEL_UNLOCK_ONE(NULL);','giant == 0'],
  'mmu4-repeat'=>['native-job.h','if (m->attempted || m->complete || m->fault != 0)','if (false)','i < 2'],
  'mmu4-no-program-on-error'=>['native-job.h','if (error == 0) {','if (true) {','bootfb_mmu4_calls() =='],
  'mmu4-first-error'=>['native-job.h','error != 0 && owner->first_error == 0','error != 0','gpu_sc.sc_bootfb.first_error == EPROTO'],
  'mmu4-primary'=>['native-job.h',"if (error == 0)\n\t\terror = m->status_error;",'error = m->status_error;','m->fault == EIO && m->status_error == EFAULT'],
  'mmu4-backing'=>['native-job.h','!m->status.backing.prepared ||','false ||','m->attempted && !m->complete && m->fault != 0'],
  'mmu4-sequence'=>['native-job.h','!m->status.sequence.sequence_complete ||','false ||','m->attempted && !m->complete && m->fault != 0'],
  'mmu4-shared'=>['native-job.h','!m->status.shared.flush_complete ||','false ||','m->attempted && !m->complete && m->fault != 0'],
  'writeback-go'=>['native-job.h','& 3U','& 2U','(writeback_control[0] & 3) == 0'],
  'writeback-busy'=>['native-job.h','& 3U','& 1U','(writeback_control[0] & 3) == 0'],
  'writeback-offset'=>['native-job.h','m->writeback_maps[i], 0x0c,','m->writeback_maps[i], 0x08,','o == 0x0c'],

  'display-complete'=>['native-job.h','!close.complete || ', '', 'display_permitted'],
  'display-closed'=>['native-job.h','!close.closed ||', 'false ||', 'display_permitted'],
  'display-drained'=>['native-job.h','!close.drained ||', 'false ||', 'display_permitted'],
  'display-error'=>['native-job.h','close.error != 0 ||', 'false ||', 'display_permitted'],
  'display-mi-error'=>['native-job.h','close.mi_error != 0 ||', 'false ||', 'display_permitted'],
  'display-md-error'=>['native-job.h','close.md_error != 0)', 'false)', 'display_permitted'],
  'display-status'=>['native-job.h','error = simplefb_console_close_status(owner->lease, &close);', 'error = 0; memset(&close,1,sizeof(close)); close.error = close.mi_error = close.md_error = 0;', 'display_permitted'],
  'display-hold'=>['native-job.h','error = wsdisplay_bootfb_serial_closed(owner->lease);', 'error = 0;', 'display_permitted'],
  'display-fw-attempt'=>['native-job.h','!owner->firmware.attempted ||', 'false ||', 'display_permitted'],
  'display-fw-complete'=>['native-job.h','!owner->firmware.complete ||', 'false ||', 'display_permitted'],
  'display-fw-fault'=>['native-job.h','owner->firmware.fault != 0)', 'false)', 'display_permitted'],
  'display-render'=>['native-job.h','owner->renderer == NULL ||', 'false ||', 'display_permitted'],
  'display-repeat'=>['native-job.h','if (d->attempted || d->complete || d->fault != 0)', 'if (false)', '!display_lock && giant == 0'],
  'display-attempt'=>['native-job.h','d->attempted = true;', '(void)d;', 'gpu_sc.sc_bootfb.display.attempted'],
  'display-giant'=>['native-job.h','KERNEL_UNLOCK_ALL(NULL, &count);', 'count = 1; KERNEL_UNLOCK_ONE(NULL);', 'giant == 0'],
  'native-bvb-inherit'=>['native-job.h','MAX(75000000U, MAX(d->bvb_status.floor, d->bvb_status.rate))', 'MAX(75000000U, d->bvb_status.floor)', 'le32toh(p[6]) >= display_firmware[i].rate'],
  'native-disp-inherit'=>['native-job.h','MAX(d->disp_status.floor, d->disp_status.rate)', 'd->disp_status.floor', 'le32toh(p[6]) >= display_firmware[i].rate'],
  'native-hsm-rate'=>['native-job.h','149985000U', '120000000U', 'display_firmware[0].rate >= 149985000'],
  'native-bvb-rate'=>['native-job.h','75000000U', '1U', 'display_firmware[1].rate >= 75000000'],
  'native-maps'=>['native-job.h',"\t3, 5, 6", "\t3, 6, 5", 'a == bcm2712_display_resources[indices[i]].memory.address'],

  'firmware-exact-length'=>['protocol.c','else if ((response & 0x80000000U) == 0)', 'else if (response != 0x80000000U)', 'gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == 0'],
  'firmware-only-four'=>['protocol.c','else if ((response & 0x80000000U) == 0)', 'else if (response != 0x80000000U && response != 0x80000004U)', 'gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == 0'],
  'firmware-response-bit'=>['protocol.c','else if ((response & 0x80000000U) == 0)', 'else if (false)', '!gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == EIO'],
  'reply-on-failure'=>['native-job.h',"if (error == 0) {\n\t\taprint_normal(\"bcmv3d: display notify", "if (true) {\n\t\taprint_normal(\"bcmv3d: display notify", 'strcmp(message, expected) == 0'],
  'reply-word-order'=>['native-job.h','le32dec(reply + 8)', 'le32dec(reply + 4)', 'strcmp(message, expected) == 0'],
  'reply-errno'=>['native-job.h', "} else {\n\t\taprint_normal(\"bcmv3d: display notify", "} else {\n\t\terror = 0;\n\t\taprint_normal(\"bcmv3d: display notify", 'strcmp(message, expected) == 0'],
  'reply-duplicate'=>['native-job.h',"\t\t    le32dec(reply + 12), le32dec(reply + 16), le32dec(reply + 20));", "\t\t    le32dec(reply + 12), le32dec(reply + 16), le32dec(reply + 20));\n\t\taprint_normal(\"duplicate\");", 'firmware_diagnostics == 1 && firmware_calls == 1'],
  'firmware-complete'=>['native-job.h','!close.complete || ', '', 'firmware_permitted'],
  'firmware-closed'=>['native-job.h','!close.closed ||', 'false ||', 'firmware_permitted'],
  'firmware-drained'=>['native-job.h','!close.drained ||', 'false ||', 'firmware_permitted'],
  'firmware-status'=>['native-job.h','close.error != 0 ||', 'false ||', 'firmware_permitted'],
  'firmware-held'=>['native-job.h','error = wsdisplay_bootfb_serial_closed(owner->lease);','error = 0;', 'firmware_permitted'],
  'firmware-transport'=>['native-job.h','error = bcmmbox_request(BCMMBOX_CHANARM2VC, packet, length, &response);','(void)bcmmbox_request(BCMMBOX_CHANARM2VC, packet, length, &response); error = 0;', 'strcmp(message, expected) == 0'],
  'firmware-malformed'=>['protocol.c','bcm2712_handoff_word(packet + 4) != 0x80000000U ||', 'false ||', 'gpu_sc.sc_bootfb.firmware.fault == expected'],
  'firmware-unsupported'=>['protocol.c','else if (response == 0)', 'else if (false)', 'gpu_sc.sc_bootfb.firmware.fault == expected'],
  'firmware-repeated-job'=>['native-job.h','if (owner->phase != BCMV3D_BOOTFB_QUEUED)', 'if (false)', 'gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED'],
      'poll-pair'=>['display.h','wsdisplay_poll_admitted = cn_publication_enter() == 0;',"const bool closed = cn_publication_closed();\n\t\twsdisplay_poll_admitted = cn_publication_enter() == 0;",'serial_on == 1 && serial_off == 1 && kbd_on == 0 && kbd_off == 0'],
      'competing-rollback'=>['display.h','wsdisplay_console_generation == wsdisplay_serial_generation &&\n\t    wsdisplay_cons.cn_dev == wsdisplay_serial_dev &&\n\t    wsdisplay_early_binding()', 'true', 'wsdisplay_console_initted == 2 && wsdisplay_cons.cn_dev == competing_dev'],
      'previous'=>['display.h','cn_bootfb_serial_hold(lease, wsdisplay_ocn, &dev)','cn_bootfb_serial_hold(lease, fixture_previous, &dev)','owner == (expect_closed'],
      'generation'=>['display.h',"\twsdisplay_cons.cn_dev = dev;\n\twsdisplay_console_generation++;", "\twsdisplay_cons.cn_dev = dev;",'wsdisplay_console_generation == 2'],
      'hold'=>['provider.inc','plcom_console_ready.held_lease = lease;','/* control omits lifetime hold */','serial_fixture_held()'],
      'barrier'=>['display.h','pserialize_perform(psz);','if (false) pserialize_perform(psz);','barriers == 1'],
      'query'=>['native-job.h','bcmv3d_bootfb_retain(sc, status_error);','bcmv3d_bootfb_cancel(sc, status_error);','cancel_error == 0'],
      'close-state'=>['issuer.c','BOOTFB_CLOSED : BOOTFB_CLAIMED;','BOOTFB_CLAIMED : BOOTFB_CLAIMED;','BOOTFB_CLOSED'],
      'getc'=>['display.h','return serial->cn_getc(serial->cn_dev);','return -1;','wsdisplay_getc(0) == 83'],
      'poll'=>['display.h','serial->cn_pollc(serial->cn_dev, on);','(void)serial;','serial_on == 1'],
      'putc'=>['display.h','serial->cn_putc(serial->cn_dev, i);','(void)serial;','serial_puts == 1'],
      'held-revoke'=>['provider.inc','if (plcom_console_ready.held_lease != NULL) {','if (false) {','maps == before_maps && hard_calls == before_irq && writes == before'],
      'held-unmap'=>['common.inc',"\tif (!plcom_console_ready_revoke(NULL)) {\n\t\tif (warm)\n\t\t\tKERNEL_UNLOCK_ONE(NULL);\n\t\treturn;","\tif (false) {\n\t\tif (warm)\n\t\t\tKERNEL_UNLOCK_ONE(NULL);\n\t\treturn;",'!plcom_console_ready.published || plcom_console_ready.revoked'],
      "held-reattach"=>['common.inc',"\tif (!plcom_console_attach_begin(sc->sc_dev, pi->pi_iot, pi->pi_iobase))", "\tif (false)",'!plcom_console_ready.published || plcom_console_ready.revoked'],
      'frontend-alias'=>['acpi.inc','if (!plcom_console_attach_begin(self, aa->aa_memt, mem->ar_base))','if (false)','maps == before_maps && hard_calls == before_irq && writes == before'],
      'foreign-cancel'=>['display.h','lease == NULL || wsdisplay_serial_lease != lease ||\n\t    cn_publication_closed()', 'cn_publication_closed()', 'wsdisplay_cons.cn_dev == serial_dev && serial_fixture_held()']
    }
    unless ENV['SKIP_MUTANTS']=='1' || ENV['COMPILE_ONLY']=='1'
      selected=ENV.fetch('SERIAL_HANDOFF_MUTANTS',controls.keys.join(',')).split(',')
      selected.each do |name|
        path,before,after,expected=controls.fetch(name);before=before.gsub('\\n', "\n").gsub('\\t', "\t");text=File.read(work+'/'+path)
        abort 'control anchor absent '+name unless text.include?(before)
        if name.start_with?('kms-detect-')
          body=function(text,'bcmv3d_bootfb_detect')
          abort 'scoped control anchor absent '+name unless body.include?(before)
          mutated=text.sub(body,body.sub(before,after))
        elsif name.start_with?('hvs-owner-') || name.start_with?('hvs-read-') || name.start_with?('hvs-write-')
          fn=name.start_with?('hvs-read-') ? 'bcmv3d_publish_read' : name.start_with?('hvs-write-') ? 'bcmv3d_publish_write' : 'bcmv3d_bootfb_publish'
          body=function(text,fn)
          abort 'scoped control anchor absent '+name unless body.include?(before)
          mutated=text.sub(body,body.sub(before,after))
        elsif name.start_with?('mmu4-') || name.start_with?('writeback-')
          fn=name.start_with?('writeback-') ? 'bcmv3d_bootfb_writeback_idle' : 'bcmv3d_bootfb_program_mmu4'
          body=function(text,fn)
          abort 'scoped control anchor absent '+name unless body.include?(before)
          mutated=text.sub(body,body.sub(before,after))
        elsif name.start_with?('display-')
          body=function(text,'bcmv3d_bootfb_stop_display')
          abort 'scoped control anchor absent '+name unless body.include?(before)
          mutated=text.sub(body,body.sub(before,after))
        else
          mutated=text.sub(before,after)
        end

        if name=='poll-pair'
          mutated.sub!('!wsdisplay_poll_admitted && cn_publication_closed()', '!wsdisplay_poll_admitted && closed')
        end
        if name=='previous'
          mutated.sub!('cn_bootfb_serial_validate(lease, wsdisplay_ocn, &dev)', 'cn_bootfb_serial_validate(lease, fixture_previous, &dev)')
        end
        File.write(work+'/'+path,mutated)
        compile.call(name); status=run.call('control-'+name,[*runner,work+'/contract'])
        reached=last_err.include?('CHECK') && last_err.include?(expected)
        abort 'control did not reach required CHECK '+name unless (status.termsig==Signal.list.fetch('ABRT') || status.exitstatus==1) && reached
        puts 'native handoff control '+name+': CHECK'
        File.write(work+'/'+path,text)
      end
    end
  ensure
    if evidence
      FileUtils.mkdir_p(evidence+'/inputs')
      Dir.children(work).reject { |n| %w[close ready contract provider.o].include?(n) }.each { |n| FileUtils.cp_r(work+'/'+n,evidence+'/inputs/'+n) }
      FileUtils.cp_r(work+'/close/source.json',evidence+'/close-source.json') if File.exist?(work+'/close/source.json')
      FileUtils.cp_r(work+'/mmu4/source.json',evidence+'/mmu4-source.json') if File.exist?(work+'/mmu4/source.json')
      FileUtils.cp_r(work+'/ready/sources.json',evidence+'/ready-source.json') if File.exist?(work+'/ready/sources.json')
      File.write(evidence+'/source.json',JSON.pretty_generate(files.transform_values { |t| Digest::SHA256.hexdigest(t) })+"\n")
    end
  end
end
