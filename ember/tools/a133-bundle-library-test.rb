#!/usr/bin/env ruby
# Origin: EmberBSD - import and real-file contracts for callable bundle verification.
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'

tool=File.join(__dir__,'a133-install-bundle.rb')
out,err,status=Open3.capture3(RbConfig.ruby,'-e','require ARGV.fetch(0); puts "IMPORTED"',tool)
abort "FAIL: requiring bundle checker ran CLI: #{out} #{err}" unless status.success? && err.empty? && out=="IMPORTED\n"
require_relative 'a133-install-bundle'
checks=1
Dir.mktmpdir('a133-bundle-api-') do |dir|
  artifacts=%w[boot resources root].zip([33554432,33554432,512]).map do |role,bytes|
    file=File.join(dir,"#{role}.bin")
    File.open(file,'wb') { |output| output.truncate(bytes) }
    {'role'=>role,'file'=>"#{role}.bin",'bytes'=>bytes,'sha256'=>Digest::SHA256.file(file).hexdigest}
  end
  manifest=File.join(dir,'manifest.json')
  File.write(manifest,JSON.generate({'schema'=>1,'board'=>'ys-m33-a133','source_commit'=>'a'*40,'artifacts'=>artifacts}))
  result=A133Bundle.verify(manifest)
  abort 'FAIL: callable verifier result' unless result[:receipt][:status]=='artifact_manifest_verified' &&
    result[:receipt][:writes_performed]==0 && result[:receipt][:installation_ready]==false &&
    result[:paths].keys.sort==%w[boot resources root] && result[:paths].all? do |role,path|
      File.identical?(path,File.join(dir,"#{role}.bin"))
    end
  checks+=1
  File.open(File.join(dir,'root.bin'),'r+b') { |file| file.write('X') }
  begin
    A133Bundle.verify(manifest)
    abort 'FAIL: API accepted changed bundle'
  rescue BundleError=>error
    abort 'FAIL: API error normalization' unless error.message=='image_sha256_mismatch'
  end
  checks+=1
  File.symlink(manifest,File.join(dir,'link'))
  begin
    A133Bundle.verify(File.join(dir,'link'))
    abort 'FAIL: API followed manifest symlink'
  rescue BundleError=>error
    abort 'FAIL: API symlink reason' unless error.message=='bundle_file_not_regular'
  end
  checks+=1
end
puts "Bundle library: #{checks} import and real-file cases passed"
