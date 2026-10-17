# Origin: EmberBSD installed desktop network regression, prepared with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require 'open3'
root=File.expand_path('../..',__dir__)
source=File.read(root+'/usr.sbin/sysinst/wizard.c')
configure=source[/bool\nwizard_configure\(void\).*?^\}/m]
abort 'Saved Ethernet settings run after desktop network setup' unless configure.index('mnt_net_config()') < configure.index('ember-desktop-install %s')
helper=File.read(root+'/libexec/ember-desktop/ember-desktop-install')
body=helper[/configure_network\(\)\n\{.*?^\}/m] or abort 'Missing installed desktop network configuration'
Dir.mktmpdir do |d|
  Dir.mkdir(d+'/etc')
  File.write(d+'/etc/rc.conf', "dhcpcd=YES\ndhcpcd_flags=\"-qM wm0\"\nwpa_supplicant_flags=\"-B -i urtwn0\"\nsshd=YES\n")
  File.write(d+'/etc/dhcpcd.conf', "hostname\n")
  File.write(d+'/etc/ifconfig.wm0', "up\n")
  File.write(d+'/etc/wpa_supplicant.conf', "ctrl_interface=/var/run/wpa_supplicant\n")
  script=body.gsub('/etc/', d+'/etc/')+"\nconfigure_network\n"
  2.times do
    out,err,st=Open3.capture3('sh','-eu','-c',script);abort out+err unless st.success?
    rc=File.read(d+'/etc/rc.conf')
    abort 'Ethernet limits Wi-Fi discovery' unless rc.scan(/^dhcpcd_flags="-qM"$/).length==1
    abort 'Saved single-radio WPA flags remain' if rc.include?('-i urtwn0')
    abort 'SSH default changed' unless rc.include?('sshd=YES')
    abort 'DHCP interface excluded' if File.read(d+'/etc/dhcpcd.conf').include?('denyinterfaces wm0')
  end
  File.write(d+'/etc/ifconfig.wm0', "up\n192.0.2.7 netmask 255.255.255.0\n")
  out,err,st=Open3.capture3('sh','-eu','-c',script);abort out+err unless st.success?
  abort 'Desktop DHCP may overwrite a saved static address' unless File.read(d+'/etc/dhcpcd.conf').include?('denyinterfaces wm0')
end
puts 'Desktop network: saved Ethernet DHCP keeps Wi-Fi visible; static addressing and SSH preserved'
