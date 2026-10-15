# Origin: EmberBSD login presentation regression, prepared with AI assistance.
require 'tmpdir'
require 'open3'

root = File.expand_path('../..', __dir__)
abort 'motd local assignments split release names in the native shell' if
  File.read(root + '/etc/rc.d/motd').match?(/local .*?=\$\(/)
abort 'SSH service is not enabled by default' unless
  File.read(root + '/etc/defaults/rc.conf').match?(/^sshd=YES\s/)
abort 'getty still exposes the compatibility system name' unless
  File.read(root + '/etc/gettytab').include?('im=\\r\\nEmberBSD/%m')
abort 'login still prints the compiled copyright by default' unless
  File.read(root + '/etc/login.conf').match?(/^default:\\\n\s*:copyright=\/dev\/null:/)
abort 'release header still selects the compatibility version' unless
  File.read(root + '/etc/Makefile').include?('echo "EmberBSD ${EMBER_VERSION}/${MACHINE}"')

Dir.mktmpdir('ember-motd-') do |dir|
  source = File.read(root + '/etc/rc.d/motd')
  body = source[/motd_start\(\)\n\{.*?\n\}/m] or abort 'missing motd function'
  body = body.gsub('/etc/', dir + '/')
  File.write(dir + '/release', "EmberBSD 1.0/amd64\n\nCopyright preserved here\nBuild information:\n            Build ID   abc123\n")
  File.write(dir + '/motd', "NetBSD 11.0 (GENERIC)\n\nWelcome to NetBSD!\nSite maintenance notice\n")
  [false, true].each do |details|
    2.times do
      script = "checkyesno() { #{details ? 'true' : 'false'}; }\n" +
        "sysctl() { echo 'unexpected kernel version query' >&2; return 99; }\n" +
        "motd_release_tag=\n" + body + "\nmotd_start\n"
      out, err, status = Open3.capture3('sh', '-c', script)
      abort "motd failed: #{out}#{err}" unless status.success?
      text = File.read(dir + '/motd')
      abort "wrong login greeting: #{text}" unless text.start_with?("EmberBSD 1.0/amd64\n")
      abort 'compatibility/copyright leaked into motd' if text.match?(/NetBSD|Copyright/)
      abort 'site notice lost or repeated' unless text.scan('Site maintenance notice').length == 1
      abort 'welcome lost or repeated' unless text.scan('Welcome to EmberBSD!').length == 1
      abort 'release Build ID not shown' if details && !text.include?('abc123')
    end
  end
end
puts 'Login branding: release identity, quiet copyright and repeatable motd pass'
