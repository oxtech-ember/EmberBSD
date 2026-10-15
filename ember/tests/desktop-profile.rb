# Origin: EmberBSD installed desktop profile regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'fileutils'
require 'open3'
root=File.expand_path('../..',__dir__)
launcher=File.read(root+'/libexec/ember-desktop/ember-desktop')
installer=File.read(root+'/libexec/ember-desktop/ember-desktop-install')
Dir.mktmpdir do |d|
  %w[etc pkg/bin cache].each{|p|FileUtils.mkdir_p(d+'/'+p)}
  %w[startxfce4 startplasma-wayland].each do |name|
    check=name=='startplasma-wayland' ? "[ -n \"${XDG_RUNTIME_DIR-}\" ] && [ -d \"$XDG_RUNTIME_DIR\" ] || exit 71\n[ \"$(stat -f %Lp \"$XDG_RUNTIME_DIR\")\" = 700 ] || exit 72\nprintf '%s\\n' \"$XDG_RUNTIME_DIR\" > \"$EMBER_RUNTIME_RECEIPT\"\n" : ''
    File.write(d+'/pkg/bin/'+name,"#!/bin/sh\n"+check+"printf '%s\\n' '#{name}'\nexit ${EMBER_PLASMA_EXIT-0}\n")
    File.chmod(0755,d+'/pkg/bin/'+name)
  end
  File.write(d+'/pkg/bin/dbus-run-session',"#!/bin/sh\n[ \"$1\" = -- ] && shift\nexec \"$@\"\n")
  File.chmod(0755,d+'/pkg/bin/dbus-run-session')
  script=launcher.gsub('/etc/ember-desktop',d+'/etc/ember-desktop').gsub('/usr/pkg',d+'/pkg')
  run=-> {Open3.capture3({'XDG_RUNTIME_DIR'=>nil,'EMBER_RUNTIME_RECEIPT'=>d+'/runtime'},'sh','-eu','-c',script)}
  out,err,st=run.call;abort out+err unless st.success? && out.strip=='startxfce4'
  File.write(d+'/etc/ember-desktop',"plasma\n")
  out,err,st=run.call;abort 'Plasma selection launched the wrong desktop: '+out+err unless st.success? && out.strip=='startplasma-wayland'
  runtime=File.read(d+'/runtime').strip
  abort 'Private runtime directory leaked after logout' if File.exist?(runtime)
  out,err,st=Open3.capture3({'XDG_RUNTIME_DIR'=>nil,'EMBER_RUNTIME_RECEIPT'=>d+'/runtime','EMBER_PLASMA_EXIT'=>'67'},'sh','-eu','-c',script)
  abort 'Plasma failure status was lost' unless st.exitstatus==67
  abort 'Private runtime directory leaked after failed startup' if File.exist?(File.read(d+'/runtime').strip)
  Dir.mkdir(d+'/shared-runtime',0700)
  out,err,st=Open3.capture3({'XDG_RUNTIME_DIR'=>d+'/shared-runtime','EMBER_RUNTIME_RECEIPT'=>d+'/runtime'},'sh','-eu','-c',script)
  abort out+err unless st.success? && File.directory?(d+'/shared-runtime')
  File.chmod(0755,d+'/shared-runtime')
  out,err,st=Open3.capture3({'XDG_RUNTIME_DIR'=>d+'/shared-runtime','EMBER_RUNTIME_RECEIPT'=>d+'/runtime'},'sh','-eu','-c',script)
  abort 'Unsafe runtime directory was accepted' if st.success? || !out.empty?
  File.write(d+'/etc/ember-desktop',"invalid\n")
  out,err,st=run.call;abort 'Unknown profile ran a desktop' if st.success? || !out.empty?
  File.write(d+'/etc/ember-desktop',"plasma\n")
  FileUtils.rm(d+'/pkg/bin/startplasma-wayland')
  out,err,st=run.call;abort 'Missing Plasma silently fell back' if st.success? || !out.empty?
  # An XFCE bundle must be rejected before any package command for Plasma.
  File.write(d+'/cache/PROFILE',"xfce\n")
  bad=installer.gsub('/var/cache/ember-desktop',d+'/cache').sub('$(id -u)','0')
  out,err,st=Open3.capture3('sh','-eu','-c',bad,'desktop-install','plasma')
  abort 'Plasma accepted a mismatched bundle' unless !st.success? && err.include?('profile')
end
puts 'PASS: installed profile selects Plasma; invalid, absent and mismatched profiles fail closed'
