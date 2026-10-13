# SPDX-License-Identifier: BSD-2-Clause
require 'tmpdir'
require_relative 'kit'

module EmberRelease
  class GitHub
    def initialize(dir, runner: nil)
      @dir = File.expand_path(dir)
      @manifest = EmberRelease.verify(@dir)
      @runner = runner || method(:run)
      @tag = @manifest.fetch('release_tag')
      @sha = @manifest.fetch('source_commit')
      @repo = "github.com/#{REPOSITORY}"
    end

    def run(*args)
      out, err, status = Open3.capture3('gh', *args)
      optional_http_response = args.first == 'api' && out.match?(/\AHTTP\/[^ ]+ 404\b/)
      EmberRelease.check(status.success? || optional_http_response, "GitHub command failed: #{err}")
      out
    end

    def api(path, missing: false)
      raw = @runner.call('api', '--include', '--hostname', 'github.com',
        '-H', 'X-GitHub-Api-Version: 2026-03-10', "repos/#{REPOSITORY}/#{path}")
      header, body = raw.split(/\r?\n\r?\n/, 2)
      code = header.to_s[/\AHTTP\/[^ ]+ (\d+)/, 1].to_i
      return nil if missing && code == 404
      EmberRelease.check(code == 200, "GitHub API refused #{path}: HTTP #{code}")
      JSON.parse(body)
    rescue JSON::ParserError => e
      raise Error, "Invalid GitHub API response: #{e.message}"
    end

    def preflight
      EmberRelease.check(api('immutable-releases')['enabled'] == true, 'Enable immutable releases before publication')
      status = api("compare/#{@sha}...main")['status']
      EmberRelease.check(%w[ahead identical].include?(status), 'The release source is not accepted in public main')
      refs = api("git/matching-refs/tags/#{@tag}")
      ref = refs.find { |r| r['ref'] == "refs/tags/#{@tag}" }
      if ref
        object = ref.fetch('object')
        EmberRelease.check(object['type'] == 'commit' && object['sha'] == @sha, 'Existing tag has another source; do not replace it')
      end
      release = find_release
      EmberRelease.check(!ref || release, 'Tag exists without the matching draft release')
      if release && !ref
        EmberRelease.check(release['draft'] == true && release['target_commitish'] == @sha,
          'Pending release tag is not bound to the exact source')
      end
      release
    end

    def find_release
      release = api("releases/tags/#{@tag}", missing: true)
      return release if release
      matches = []
      page = 1
      loop do
        batch = api("releases?per_page=100&page=#{page}")
        EmberRelease.check(batch.is_a?(Array), 'Invalid release list')
        matches.concat(batch.select { |r| r['tag_name'] == @tag })
        break if batch.length < 100
        page += 1
      end
      EmberRelease.check(matches.length <= 1, 'Multiple releases reserve the same tag')
      return nil if matches.empty?
      release = api("releases/#{matches.first.fetch('id')}")
      EmberRelease.check(release['tag_name'] == @tag, 'Release tag changed during lookup')
      release
    end

    def names
      @manifest['files'].map { |f| f['name'] } + %w[manifest.json SHA256]
    end

    def title
      "EmberBSD 1.0 amd64 #{@sha[0, 12]} (no X11 VM candidate)"
    end

    def check_assets(release, complete:)
      assets = release.fetch('assets')
      existing = assets.map { |a| a['name'] }
      EmberRelease.check(existing.uniq == existing && (existing - names).empty?, 'Draft has unexpected or duplicate assets')
      EmberRelease.check((names - existing).empty?, 'Release assets are incomplete') if complete
      assets.each do |asset|
        path = File.join(@dir, asset['name'])
        hash = Digest::SHA256.file(path).hexdigest
        EmberRelease.check(asset['state'] == 'uploaded' && asset['size'] == File.size(path) &&
          asset['digest'] == "sha256:#{hash}", "Remote asset differs: #{asset['name']}; do not clobber it")
      end
      names - existing
    end

    def draft
      release = preflight
      if release
        check_draft(release)
      else
        puts "Create private draft #{@tag}"
        @runner.call('release', 'create', @tag, '--repo', @repo, '--target', @sha, '--draft',
          '--prerelease', '--latest=false', '--title', title, '--notes-file', File.join(@dir, 'release-notes.md'))
        release = preflight
        EmberRelease.check(release && release['draft'] == true, 'Draft creation is not confirmed')
        check_draft(release)
      end
      missing = check_assets(release, complete: false)
      unless missing.empty?
        puts "Upload #{missing.length} assets to the private draft"
        @runner.call('release', 'upload', @tag, '--repo', @repo, *missing.map { |n| File.join(@dir, n) })
      end
      verify_remote
      "https://github.com/#{REPOSITORY}/releases/tag/#{@tag}"
    end

    def check_draft(release)
      EmberRelease.check(release['draft'] == true, 'Release is already published; no asset writes are allowed')
      EmberRelease.check(release['prerelease'] == true && release['name'] == title &&
        release['body'] == File.read(File.join(@dir, 'release-notes.md')), 'Existing draft does not belong to this kit')
    end

    def verify_remote
      release = find_release
      EmberRelease.check(release, 'Release is missing')
      check_assets(release, complete: true)
      puts 'Download the full release and verify every file'
      Dir.mktmpdir('ember-release-download') do |dir|
        @runner.call('release', 'download', @tag, '--repo', @repo, '--dir', dir,
          *names.flat_map { |n| ['--pattern', n] })
        EmberRelease.verify(dir)
        names.each do |name|
          EmberRelease.check(Digest::SHA256.file(File.join(dir, name)).hexdigest ==
            Digest::SHA256.file(File.join(@dir, name)).hexdigest, "Downloaded asset differs: #{name}")
        end
      end
      if release['draft'] == false
        EmberRelease.check(release['immutable'] == true, 'Published release is not immutable')
        @runner.call('release', 'verify', @tag, '--repo', @repo)
        names.each { |n| @runner.call('release', 'verify-asset', @tag, File.join(@dir, n), '--repo', @repo) }
      end
      release
    end

    def publish
      EmberRelease.require_qualification(@manifest)
      release = preflight
      EmberRelease.check(release, 'Create and verify the draft first')
      check_draft(release) if release['draft'] == true
      release = verify_remote
      if release['draft'] == true
        check_draft(release)
        puts 'Publish the verified immutable prerelease'
        @runner.call('release', 'edit', @tag, '--repo', @repo, '--draft=false', '--prerelease', '--latest=false')
        release = find_release
      end
      EmberRelease.check(release['draft'] == false && release['immutable'] == true, 'Immutable publication is not confirmed')
      @runner.call('release', 'verify', @tag, '--repo', @repo)
      names.each { |n| @runner.call('release', 'verify-asset', @tag, File.join(@dir, n), '--repo', @repo) }
      "https://github.com/#{REPOSITORY}/releases/tag/#{@tag}"
    end
  end
end
