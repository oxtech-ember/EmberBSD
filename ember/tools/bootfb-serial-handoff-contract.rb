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
  a=text.scan(/^(?:static )?(?:void|int|bool)\n#{Regexp.escape(name)}\(.*?^}/m)
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
  sys/arch/arm/broadcom/bcm2835_mbox.c sys/arch/arm/broadcom/bcm2835_mbox.h]
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
    File.write(work+'/bcm2712_display_handoffvar.h',files.fetch('sys/arch/arm/broadcom/bcm2712_display_handoffvar.h'))
    File.write(work+'/protocol.c',files.fetch('sys/arch/arm/broadcom/bcm2712_display_handoff.c'))
    helpers=%w[bcmv3d_bootfb_exchange bcmv3d_bootfb_notify bcmv3d_bootfb_claim_job bcmv3d_bootfb_queue bcmv3d_bootfb_retain bcmv3d_bootfb_cancel bcmv3d_finalize]
    File.write(work+'/native-job.h',notice(acpi)+"static void bcmv3d_bootfb_retain(struct bcmv3d_softc *, int);\nstatic void bcmv3d_bootfb_cancel(struct bcmv3d_softc *, int);\n"+helpers.map { |n| function(acpi,n) }.join("\n"))
    ws=File.read(source+'/sys/dev/wscons/wsdisplay.c')
    commit=ws[/^\t\twsdisplay_cons.cn_dev = makedev\(maj,\n\t\t    WSDISPLAYMINOR\(device_unit\(self\), 0\)\);\n\t\twsdisplay_console_generation\+\+;/]
    abort 'actual cdev commit missing' unless commit
    File.write(work+'/native-cdev-commit.h',notice(ws)+"static void fixture_cdev_commit(device_t self, int maj)\n{\n"+commit+"\n}\n")
    File.write(work+'/native-cnopen.h',notice(files.fetch(paths[2]))+function(files.fetch(paths[2]),'cnopen').sub('int mode, struct lwp *l)', 'int mode __attribute__((__unused__)), struct lwp *l __attribute__((__unused__)))'))
    # The real ACPI attachment must use the actual common activate guard only on.
    front=File.read(source+'/sys/arch/arm/acpi/plcom_acpi.c')
    abort 'conditional provider guard missing' unless front.include?("#ifdef WSDISPLAY_BOOTFB_HANDOFF\n    plcom_activate);\n#else\n    NULL);")
    cc=Shellwords.split(ENV.fetch('CC','cc'));flags=Shellwords.split(ENV.fetch('CFLAGS',''))
    declaration=front[/^CFATTACH_DECL_NEW\(plcom_acpi,.*?^#endif/m]
    abort 'actual attachment declaration missing' unless declaration
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
    compile=lambda do |name|
      common=[*cc,'-std=c11','-Wall','-Wextra','-Werror','-pthread',*flags,'-I'+work,'-I'+source+'/ember/tools']
      s=run.call('compile-provider-'+name,[*common,'-DSERIAL_READY_LIBRARY','-Dmain=serial_ready_original_main','-c',source+'/ember/tools/serial-console-ready-contract.c','-o',work+'/provider.o'])
      abort 'provider compile failed' unless s.success?
      s=run.call('compile-route-'+name,[*common,'-DBCM2712_V3D_RENDER','-DBCM2712_V3D_TAKEOVER',source+'/ember/tools/bootfb-serial-handoff-contract.c',work+'/protocol.c',work+'/provider.o','-o',work+'/contract'])
      abort 'route compile failed' unless s.success?
    end
    compile.call('positive')
    unless ENV['COMPILE_ONLY']=='1'
      abort 'native handoff positive failed' unless run.call('positive',[*runner,work+'/contract']).success?
    end
controls={
  'firmware-complete'=>['native-job.h','!close.complete || ', '', 'firmware_permitted'],
  'firmware-closed'=>['native-job.h','!close.closed ||', 'false ||', 'firmware_permitted'],
  'firmware-drained'=>['native-job.h','!close.drained ||', 'false ||', 'firmware_permitted'],
  'firmware-status'=>['native-job.h','close.error != 0 ||', 'false ||', 'firmware_permitted'],
  'firmware-held'=>['native-job.h','error = wsdisplay_bootfb_serial_closed(owner->lease);','error = 0;', 'firmware_permitted'],
  'firmware-transport'=>['native-job.h','return bcmmbox_request(BCMMBOX_CHANARM2VC, packet, length, &response);','(void)bcmmbox_request(BCMMBOX_CHANARM2VC, packet, length, &response); return 0;', 'gpu_sc.sc_bootfb.firmware.fault == errors[i]'],
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
        mutated=text.sub(before,after)
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
      FileUtils.cp_r(work+'/ready/sources.json',evidence+'/ready-source.json') if File.exist?(work+'/ready/sources.json')
      File.write(evidence+'/source.json',JSON.pretty_generate(files.transform_values { |t| Digest::SHA256.hexdigest(t) })+"\n")
    end
  end
end
