#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Actual ACPI/issuer/pending/renderer chain. Only native services are controlled.
require 'tmpdir'
require 'fileutils'
require 'shellwords'
require 'json'
require 'rbconfig'
require 'digest'
require 'open3'
require 'timeout'
source = File.realpath(ARGV[0] || File.expand_path('../..', __dir__))
evidence = ENV['CLAIM_JOB_EVIDENCE']
abort 'new evidence directory required' if evidence && File.exist?(evidence)
FileUtils.mkdir_p(evidence) if evidence
commands = []
last_error = ''
run = lambda do |name, argv, env = {}|
  out = err = ''; status = nil
  Open3.popen3(env, *argv, pgroup: true) do |i, o, e, waiter|
    i.close; a = Thread.new { o.read }; b = Thread.new { e.read }
    begin
      Timeout.timeout(30) { status = waiter.value }
    rescue Timeout::Error
      Process.kill('KILL', -waiter.pid); status = waiter.value
    end
    out = a.value; err = b.value
  end
  commands << { name: name, argv: argv, env: env, status: status.exitstatus, signal: status.termsig }
  if evidence
    File.write(evidence + '/' + name + '.stdout', out)
    File.write(evidence + '/' + name + '.stderr', err)
    File.write(evidence + '/commands.json', JSON.pretty_generate(commands) + "\n")
  end
  last_error = err
  print out; warn err unless err.empty?
  status
end
def function(text, name)
  found = text.scan(/^(?:static )?(?:int|void|bool)\n#{Regexp.escape(name)}\(.*?^}/m)
  abort "actual function count #{name}: #{found.size}" unless found.size == 1
  found.first
end
def notice(text)
  text.split(/^#(?:include|ifndef)/, 2).first
end
paths = %w[sys/arch/arm/broadcom/bcm2712_v3d_acpi.c sys/arch/arm/broadcom/bcm2712_v3d_render.h
           sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.c sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.h
           sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h
           sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h sys/kern/subr_autoconf.c
           ember/tools/bootfb-issuer-contract.c ember/tools/bootfb-issuer-contract.rb
           ember/tools/bootfb-claim-job-contract.c ember/tools/bootfb-claim-job-contract.rb]
files = paths.to_h { |p| [p, File.binread(source + '/' + p)] }
Dir.mktmpdir('bootfb-claim-job-', ENV.fetch('TMPDIR', '/tmp')) do |work|
  puts 'claim job temporary directory: ' + work
  begin
    s = run.call('export-issuer', [RbConfig.ruby, source + '/ember/tools/bootfb-issuer-contract.rb', source],
      {'BOOTFB_SKIP_POSITIVES'=>'1', 'BOOTFB_SKIP_MUTANTS'=>'1', 'BOOTFB_EVIDENCE'=>work+'/issuer-export'})
    abort 'issuer export failed' unless s.success?
    FileUtils.cp_r(work + '/issuer-export/inputs/.', work)
    FileUtils.mkdir_p(work + '/dev/acpi'); File.write(work + '/dev/acpi/acpivar.h', '')
    FileUtils.mkdir_p(work + '/arm/broadcom')
    File.write(work + '/arm/broadcom/bcm2712_v3d_render.h', files.fetch(paths[1]))
    issuer = files.fetch('ember/tools/bootfb-issuer-contract.c')
    abort 'issuer entry count' unless issuer.scan('int main(void)').size == 1
    File.write(work + '/issuer-fixture.h', issuer.sub('int main(void)', 'int bootfb_issuer_original_main(void)'))
    acpi = files.fetch(paths[0])
    state = acpi[/^#define BCMV3D_HUB\b.*?^    bcmv3d_match, bcmv3d_attach, NULL, NULL\);/m]
    abort 'ACPI state missing' unless state
    File.write(work + '/acpi-state.h', notice(acpi) + state + "\n")
    helpers = %w[bcmv3d_bootfb_claim_job bcmv3d_bootfb_queue bcmv3d_bootfb_retain bcmv3d_bootfb_cancel]
    body = "#ifdef WSDISPLAY_BOOTFB_DEFER\n" + helpers.map { |n| function(acpi, n) }.join("\n") + "\n#endif\n"
    body += %w[bcmv3d_finalize bcmv3d_match bcmv3d_attach].map { |n| function(acpi, n) }.join("\n")
    File.write(work + '/acpi-functions.h', notice(acpi) + body)
    auto = files.fetch('sys/kern/subr_autoconf.c')
    state = auto[/^struct deferred_config \{.*?^TAILQ_HEAD\(deferred_config_head, deferred_config\);/m]
    queue = auto[/^static struct deferred_config_head interrupt_config_queue =\n\s*TAILQ_HEAD_INITIALIZER\(interrupt_config_queue\);/]
    abort 'actual queue absent' unless state && queue
    body = %w[config_pending_incr config_pending_decr config_interrupts config_interrupts_thread config_process_deferred].map { |n| function(auto, n) }.join("\n")
    File.write(work + '/interrupt-functions.h', notice(auto) + state + "\n" + queue + "\n" + body)
    drv = files.fetch('sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.h')
    type = drv[/^struct bcmv3d_device \{.*?^};/m]
    constants = drv.lines.grep(/^#define BCMV3D_VA_(START|SIZE)\b/).join
    param = files.fetch('sys/external/bsd/drm2/dist/include/uapi/drm/v3d_drm.h')[/^enum drm_v3d_param \{.*?^};/m]
    abort 'actual renderer types absent' unless type && param
    File.write(work + '/bcmv3d_bo_stats.h', files.fetch('sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h'))
    File.write(work + '/renderer-types.h', notice(drv) + constants + type + "\n" + param + "\n")
    render = files.fetch('sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.c')
    File.write(work + '/renderer-function.h', notice(render) + function(render, 'bcmv3d_render_attach'))
    cc = Shellwords.split(ENV.fetch('CC', 'cc')); flags = Shellwords.split(ENV.fetch('CFLAGS', ''))
    prefix = ENV['TEST_RUNNER'] ? [ENV.fetch('TEST_RUNNER')] : []
    compile = lambda do |label, enabled|
      argv = [*cc, '-std=c11', '-Wall', '-Werror', '-pthread', *flags,
        '-DBOOTFB_ISSUER_CONTRACT', '-DBOOTFB_CLAIM_JOB_CONTRACT', '-DWSDISPLAY_EARLYFB_HOOKS',
        '-DBCM2712_V3D_RENDER', '-DBCM2712_V3D_TAKEOVER', *(enabled ? ['-DWSDISPLAY_BOOTFB_DEFER'] : []),
        '-I'+work, '-I'+source+'/ember/tools', work+'/simplefb_subr.c',
        source+'/ember/tools/bootfb-claim-job-contract.c', '-o', work+'/contract']
      abort 'compile failed '+label unless run.call('compile-'+label, argv).success?
    end
    unless ENV['CLAIM_JOB_SKIP_POSITIVES'] == '1'
      [false, true].each do |enabled|
        label = enabled ? 'on' : 'off'; compile.call(label, enabled)
        next if ENV['CLAIM_JOB_COMPILE_ONLY'] == '1'
        abort 'positive failed '+label unless run.call('positive-'+label, [*prefix, work+'/contract']).success?
      end
    end
    unless ENV['CLAIM_JOB_SKIP_MUTANTS'] == '1' || ENV['CLAIM_JOB_COMPILE_ONLY'] == '1'
      controls = {
        'success-cancel' => ['acpi-functions.h', 'bcmv3d_finalize', "\t\t\tbcmv3d_bootfb_cancel(sc, error);\n#endif\n\t\t\tif (error", "\t\t\tif (error != 0)\n\t\t\t\tbcmv3d_bootfb_cancel(sc, error);\n#endif\n\t\t\tif (error", 'genfb_attaches == 1'],
        'queue-after' => ['acpi-functions.h', 'bcmv3d_bootfb_queue', "owner->phase = BCMV3D_BOOTFB_QUEUED;\n\tconfig_interrupts(sc->sc_dev, bcmv3d_bootfb_claim_job);", "config_interrupts(sc->sc_dev, bcmv3d_bootfb_claim_job);\n\towner->phase = BCMV3D_BOOTFB_QUEUED;", 'sc_bootfb.phase == BCMV3D_BOOTFB_QUEUED'],
        'warm-queue' => ['acpi-functions.h', 'bcmv3d_bootfb_queue', '!cold ||', 'false ||', 'queue_calls == 0'],
        'owner-argument' => ['acpi-functions.h', 'bcmv3d_finalize', 'bootfb = &sc->sc_bootfb;', 'bootfb = NULL;', 'flags == 0 && s->bootfb == expected_owner'],
        'owner-store' => ['renderer-function.h', 'bcmv3d_render_attach', 'sc->bootfb = bootfb;', 'sc->bootfb = NULL;', 'flags == 0 && s->bootfb == expected_owner'],
        'observe-cancel' => ['acpi-functions.h', 'bcmv3d_finalize', "\t\tbcmv3d_bootfb_cancel(sc, error);\n#endif\n\t}\n#ifdef BCM2712_V3D_TAKEOVER", "\t\tbcmv3d_bootfb_retain(sc, error);\n#endif\n\t}\n#ifdef BCM2712_V3D_TAKEOVER", 'gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CANCELED && genfb_attaches == 1'],
        'takeover-retain' => ['acpi-functions.h', 'bcmv3d_finalize', "if (error != 0)\n\t\t\tbcmv3d_bootfb_retain(sc, error);", "if (error != 0)\n\t\t\tbcmv3d_bootfb_cancel(sc, error);", 'gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_RETAINED && wsdisplay_bootfb.phase == BOOTFB_RETAINED'],
        'cancel-error' => ['acpi-functions.h', 'bcmv3d_bootfb_cancel', 'if (owner->cancel_error != 0)', 'if (false)', 'gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_RETAINED && gpu_sc.sc_bootfb.first_error == EIO'],
        'pending-join' => ['finalizer-functions.h', 'config_finalize', 'while (!TAILQ_EMPTY(&config_pending))', 'while (false)', 'gpu.dv_pending == 0']
      }
      selected = ENV['CLAIM_JOB_MUTANTS'] ? ENV.fetch('CLAIM_JOB_MUTANTS').split : controls.keys
      saved = Dir.glob(work+'/**/*').select { |p| File.file?(p) && !p.include?('/issuer-export/') && File.basename(p) != 'contract' }.to_h { |p| [p, File.binread(p)] }
      begin
        selected.each do |name|
          saved.each { |p, bytes| File.binwrite(p, bytes) }
          file, fn, before, after, expected = controls.fetch(name)
          text = File.read(work+'/'+file); body = function(text, fn)
          abort 'control anchor count '+name unless body.scan(before).size == 1
          File.write(work+'/'+file, text.sub(body, body.sub(before, after)))
          if evidence
            FileUtils.mkdir_p(evidence+'/controls/'+name)
            FileUtils.cp(work+'/'+file, evidence+'/controls/'+name+'/'+file)
          end
          compile.call('control-'+name, true)
          status = run.call('control-'+name, [*prefix, work+'/contract'])
          reached = last_error.lines.any? { |line| line.start_with?('check failed ') && line.include?(expected) }
          abort 'control did not reach expected CHECK '+name unless status.termsig == Signal.list.fetch('ABRT') && reached
          puts 'claim job: rejected '+name
        end
      ensure
        saved.each { |p, bytes| File.binwrite(p, bytes) }
      end
    end

  ensure
    if evidence
      FileUtils.mkdir_p(evidence+'/inputs')
      Dir.glob(work+'/**/*').select { |p| File.file?(p) && !p.include?('/issuer-export/') && File.basename(p) != 'contract' }.each do |p|
        to=evidence+'/inputs/'+p.delete_prefix(work+'/');FileUtils.mkdir_p(File.dirname(to));FileUtils.cp(p,to)
      end
      files.each do |p, bytes|
        to=evidence+'/source-inputs/'+p;FileUtils.mkdir_p(File.dirname(to));File.binwrite(to,bytes)
      end
      File.write(evidence+'/source.json', JSON.pretty_generate({source_sha256:files.transform_values { |v| Digest::SHA256.hexdigest(v) },
        scope:'Actual issuer/finalizer/ARM reader reused; real configintr/pending/join, ACPI attach/job/finalize, renderer attach and type body. Only services controlled; fixture entry renamed, production functions unchanged. No MP/hardware/close/drain proof.'})+"\n")
    end
  end
end
