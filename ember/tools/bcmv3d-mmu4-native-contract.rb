#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
require 'digest'
require 'fileutils'
require 'json'
require 'open3'
require 'shellwords'
require 'tmpdir'
source = File.realpath(ARGV.fetch(0, File.join(__dir__, '../..')))
timeout_seconds = Integer(ENV.fetch('MMU4_NATIVE_TIMEOUT','60'))
abort 'MMU4_NATIVE_TIMEOUT must be positive' unless timeout_seconds.positive?
Process.setrlimit(Process::RLIMIT_CORE,0,0)
work = Dir.mktmpdir('bcmv3d-mmu4-native-', ENV.fetch('TMPDIR','/tmp'))
puts "Native MMU4 temporary directory: #{work}"
commands = []; inputs = {}
read = lambda do |path|
  value=File.binread(File.join(source,path));inputs[path]=Digest::SHA256.hexdigest(value);value
end
write = lambda do |name,value|
  path=File.join(work,name);FileUtils.mkdir_p(File.dirname(path));File.binwrite(path,value)
end
strip = ->(s) { s.lines.reject { |l| l.start_with?('#include ','__KERNEL_RCSID(') }.join }
run = lambda do |name,argv,required=true,env={},limit=timeout_seconds|
  out='';err='';timed_out=false;status=nil
  Open3.popen3(env,*argv,pgroup:true) do |input,output,error,wait|
    input.close
    readers=[Thread.new { out << output.read },Thread.new { err << error.read }]
    unless wait.join(limit)
      timed_out=true
      begin Process.kill('TERM',-wait.pid); rescue Errno::ESRCH; end
      unless wait.join(1)
        begin Process.kill('KILL',-wait.pid); rescue Errno::ESRCH; end
        abort "#{name}: process group did not terminate" unless wait.join(5)
      end
    end
readers.each do |reader|
      next if reader.join(1)
      timed_out=true
      begin Process.kill('KILL',-wait.pid); rescue Errno::ESRCH; end
      output.close unless output.closed?
      error.close unless error.closed?
      reader.kill unless reader.join(1)
    end
    status=wait.value
  end
  write.call(name+'.stdout',out);write.call(name+'.stderr',err)
  commands << {name:name,argv:argv,environment:env,status:status.exitstatus,
               signal:status.termsig,timed_out:timed_out,timeout_seconds:limit}
  abort "#{name} failed#{timed_out ? ' (timeout)' : ''}: #{err}" if required && (timed_out || !status.success?)
  [out,err,status,timed_out]
end
fatal_check = lambda do |stderr,status,timed_out|
  fatal=status.signaled? && status.termsig==Signal.list.fetch('ABRT')
  fatal ||= status.exitstatus==128+Signal.list.fetch('ABRT') && !ENV.fetch('TEST_RUNNER','').empty?
  !timed_out && fatal && stderr.include?('ICAC contract failed')
end
begin
  # Export the existing actual ICAC/query/cache/MMU4 and backing services.
  run.call('icac-export',['sh',File.join(source,'ember/tools/bcm2712-iommuc-native-contract.sh'),source],true,
    {'ICAC_NATIVE_COMPILE_ONLY'=>'1','ICAC_NATIVE_EVIDENCE'=>work+'/icac','BACKING_PAGES'=>'4096'})
  Dir[work+'/icac/*.h'].each { |p| FileUtils.cp(p,work) }
  services=read.call('ember/tools/bcm2712-iommuc-native-contract.c')
  anchor="int\nmain(void)";abort 'ICAC fixture main changed' unless services.scan(anchor).size==1
  write.call('icac-services.h',services.sub(anchor,"int\nicac_reference_main(void)"))
  drv='sys/external/bsd/drm2/bcmv3d/'
  {'drv-types.h'=>'bcmv3d_drv.h','bo-stats.h'=>'bcmv3d_bo_stats.h',
   'backing-types.h'=>'bcmv3d_mmu4_backing.h','adapter-types.h'=>'bcmv3d_mmu4_native.h',
   'adapter-body.h'=>'bcmv3d_mmu4_native.c'}.each { |name,path| write.call(name,strip.call(read.call(drv+path))) }
  fixture=File.join(source,'ember/tools/bcmv3d-mmu4-native-contract.c')
  read.call('ember/tools/bcmv3d-mmu4-native-contract.c')
  read.call('ember/tools/bcmv3d-mmu4-native-backing.c')
  read.call('ember/tools/bcmv3d-mmu4-native-contract.rb')
  cc=Shellwords.split(ENV.fetch('CC','cc'));flags=Shellwords.split(ENV.fetch('CFLAGS',''))
  executor=Shellwords.split(ENV.fetch('TEST_RUNNER',''))
  common=cc+%w[-std=c11 -Wall -Wextra -Werror]+flags
  back=work+'/icac/backing'
  objects=%w[bcmv3d_mmu4_backing.c bcmv3d_hvs_prepare.c arm/broadcom/bcm2712_mmu4_table.c].map do |name|
    out=work+'/'+File.basename(name,'.c')+'.o'
    run.call('compile-'+File.basename(name),common+['-I'+back,'-c',back+'/'+name,'-o',out]);out
  end
  out=work+'/bridge.o'
  run.call('compile-bridge',common+['-I'+back,'-I'+source+'/ember/tools','-c',source+'/ember/tools/bcmv3d-mmu4-native-backing.c','-o',out]);objects<<out
  acpica=ENV.fetch('ACPICA_INCLUDE',source+'/sys/external/bsd/acpica/dist/include')
  compile=lambda do |name|
    run.call(name+'-compile',common+['-I'+work,'-I'+acpica,'-I'+source+'/ember/tools','-MD','-MF',work+'/'+name+'.d',fixture,*objects,'-o',work+'/contract'])
  end
  compile.call('positive')
  FileUtils.cp(work+'/contract',ENV['MMU4_NATIVE_OUTPUT']) if ENV['MMU4_NATIVE_OUTPUT']
  unless ENV['MMU4_NATIVE_COMPILE_ONLY']=='1'
    out,=run.call('positive',executor+[work+'/contract']);print out
    # Harness-only negative controls cannot count as source mutant rejection.
    %w[segv abort hang check].each do |control|
      _,stderr,status,timed_out=run.call('harness-'+control,executor+[work+'/contract'],false,
        {'MMU4_NATIVE_HARNESS_CONTROL'=>control},control=='hang' ? 1 : timeout_seconds)
      expected=control=='check'
      abort "invalid harness classification #{control}" unless fatal_check.call(stderr,status,timed_out)==expected
      abort 'hang escaped timeout' if control=='hang' && !timed_out
      puts "Harness #{control}: #{expected ? 'causal CHECK' : 'not a causal reject'}"
    end
    unless ENV['MMU4_NATIVE_SKIP_MUTANTS']=='1'
      original=File.read(work+'/adapter-body.h')
      mutations={
        'stale-backing'=>["status_error = bcmv3d_mmu4_backing_status(n->backing,\n\t    &n->status.backing);", 'status_error = 0;', 'cached.backing.prepared == actual.prepared'],
        'backing-valid'=>["n->status.backing_valid = false;\n\tstatus_error = bcmv3d_mmu4_backing_status", "n->status.backing_valid = true;\n\tstatus_error = bcmv3d_mmu4_backing_status"],
        'backing-diagnostic'=>['if (n->status.status_error == 0)', 'if (true)'],
        'backing-primary'=>["if (error == 0)\n\t\t\terror = status_error;\n\t}\n\tif (error == 0 && n->status.fault", "error = status_error;\n\t}\n\tif (error == 0 && n->status.fault"],
        'context'=>['if (cpu_intr_p() || cpu_softintr_p())','if (false)'],
        'device-private'=>['sc->drm->dev_private != sc ||','false ||'],
        'native-driver'=>['!device_is_a(sc->dev, "bcmv3d") ||','false ||'],
        'native-attachment'=>['strcmp(attachment->ca_name, "bcmv3d_acpi") != 0','false'],
        'canonical'=>['gpu != sc->handle ||','false ||'],
        'attached'=>['node->ad_handle != gpu || node->ad_device != sc->dev','node->ad_handle != gpu || false'],
        'slot'=>['error = sc->mmu4_native == NULL ? 0 : EBUSY;','error = 0;'],
        'slot-race'=>['if (sc->mmu4_native != NULL) {','if (false) {'],
        'provider-attempt'=>['if (status.mmu4.attempted)','if (false)'],
        'observe-result'=>["if (error != 0)\n\t\t\treturn error;\n\t\tif (snapshot_error", "if (false)\n\t\t\treturn error;\n\t\tif (snapshot_error"],
        'prefix-count'=>['o->reads_attempted != 1','false'],
        'prefix-map'=>['!o->started || !o->mapped ||','!o->started || false ||'],
        'prefix-known'=>['if ((o->raw_control & 0x4U) != 0)', 'if ((o->raw_control & ~0x7U) != 0) return EOPNOTSUPP; if ((o->raw_control & 0x4U) != 0)', 'error==0 && owner->status.stage==BCMV3D_MMU4_NATIVE_PREPARED'],
        'prefix-busy'=>['(o->raw_control & 0x4U) != 0','false'],
        'prefix-order'=>['error = mmu4_native_prefix(n, true);','error = 0;'],
        'map-address'=>['MMU4_ADDRESS, MMU4_LENGTH, 0, &n->bsh','MMU4_ADDRESS + 4, MMU4_LENGTH, 0, &n->bsh'],
        'map-length'=>['MMU4_ADDRESS, MMU4_LENGTH, 0, &n->bsh','MMU4_ADDRESS, MMU4_LENGTH - 4, 0, &n->bsh'],
        'debug-offset'=>['n->bsh, 0x38);','n->bsh, 0x3c);'],
        'program-once'=>['if (n->status.program_attempted) {','if (false) {'],
        'recheck'=>['error = mmu4_native_prefix(n, false);','error = 0;'],
        'seal'=>['error = bcmv3d_mmu4_backing_seal(n->backing);','error = 0;'],
        'native-fault'=>["if (n->status.fault != 0)\n\t\treturn n->status.fault;", "if (false)\n\t\treturn n->status.fault;"],
        'core-fault'=>['if (n->active->status.fault != 0)', 'if (false)'],
        'shared-fault'=>['if (n->cache->status.fault != 0)', 'if (false)'],
        'retained-lease'=>['(n->cache->status.retained & MMU4_MEMBER) == 0', 'false'],
        'write-latch'=>['n->status.write_attempted = true;','n->status.write_attempted = false;'],
        'offset'=>['value = 0x24;','value = 0x28;'],
        'barrier'=>['n->bsh, 0, MMU4_LENGTH,','n->bsh, 0, 4,'],
        'shared-error'=>["if (error == 0)\n\t\t\terror = status_error;\n\t}\ndone:", "if (false)\n\t\t\terror = status_error;\n\t}\ndone:"],
        'protocol-fault'=>["if (!n->executing)\n\t\treturn mmu4_native_fault(n, EPROTO);", "if (!n->executing)\n\t\treturn EPROTO;"],
        'snapshot'=>['close_error = bcm2712_mmu4_core_fini(&sequence);','close_error = bcm2712_mmu4_core_fini(&sequence); n->status.sequence = sequence.status;'],
        'primary'=>['return error != 0 ? error : close_error;','return close_error != 0 ? close_error : error;'],
        'borrow-clear'=>['n->cache = NULL;','/* Keep stale borrowed pointer. */']
      }
      selected=ENV.fetch('MMU4_NATIVE_MUTANTS',mutations.keys.join(' ')).split
      selected.each do |name|
        old,new_value,expected_check=mutations.fetch(name);abort "ambiguous #{name}" unless original.scan(old).size==1
        write.call('adapter-body.h',original.sub(old,new_value));compile.call('mutant-'+name)
        _,err,status,timed_out=run.call('mutant-'+name,executor+[work+'/contract'],false)
        abort "#{name} was not a compiled fatal CHECK" unless fatal_check.call(err,status,timed_out)
        abort "#{name} missed its expected CHECK" if expected_check && !err.include?(expected_check)
        puts "Rejected compiled mutant #{name}"
      end
      write.call('adapter-body.h',original)
    end
  end
ensure
  write.call('commands.json',JSON.pretty_generate(commands)+"\n")
  write.call('source.json',JSON.pretty_generate({sha256:inputs,extraction:'Complete actual C/H bodies; only include/RCS lines removed. Existing ICAC fixture main alone is renamed. Actual provider/query/cache/MMU4/backing/helper/encoder are reused.',scope:'Controlled native ACPI/device/mutex/DRM/bus/heap services. Cached/observation wrappers inject errors/race using real provider functions; fini wrapper injects a retained lease for close-error precedence. A shared-status wrapper injects its final snapshot error. Backing create bridge validates the adapter device boundary then uses the existing controlled render/DMA/pmap model. The prepare wrapper invokes unchanged actual prepare and verifies no allocation/full five-map sync. Each child is a fresh boot; no production unmap/free/reset exists.'})+"\n")
  if ENV['MMU4_NATIVE_EVIDENCE']
    dest=ENV.fetch('MMU4_NATIVE_EVIDENCE');FileUtils.mkdir_p(dest);FileUtils.cp_r(Dir[work+'/*'],dest)
  end
  FileUtils.remove_entry(work)
end
