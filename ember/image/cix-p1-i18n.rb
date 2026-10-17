#!/usr/bin/env ruby
# SPDX-License-Identifier: BSD-2-Clause
# Host-only native DATA inventory. This does not generate conversion tables.
require 'digest'
require 'fileutils'
require 'find'
require 'json'

module CixI18n
  REQUIRED = %w[iconv/iconv.dir esdb/esdb.dir esdb/esdb.alias
    esdb/ISO646/ISO646-US.esdb esdb/UTF/UTF-8.esdb csmapper/mapper.dir
    csmapper/ISO646/ISO646-US%UCS.646].map { |p| 'usr/share/i18n/' + p }.freeze
  ALLOWED = (REQUIRED + %w[esdb/esdb.dir.db esdb/esdb.alias.db csmapper/mapper.dir.db
    csmapper/charset.pivot csmapper/charset.pivot.pvdb].map { |p| 'usr/share/i18n/' + p }).freeze
  INVENTORY = 'I18N-INVENTORY.json'

  class DuplicateKey < StandardError; end
  class UniqueHash < Hash
    def []=(key, value)
      raise DuplicateKey, 'Duplicate JSON key' if key?(key)
      super
    end
  end

  def self.parse(text)
    options = { object_class: UniqueHash, allow_duplicate_key: false }
    # Old parsers use the setter; newer parsers need the explicit option.
    # Probe the actual host parser so an ignored option cannot weaken checks.
    begin
      JSON.parse('{"duplicate":0,"duplicate":1}', **options)
    rescue JSON::ParserError, DuplicateKey
      return JSON.parse(text, **options)
    end
    raise 'Host JSON parser does not reject duplicate keys'
  end

  def self.regular(path)
    raise "Not a regular file: #{path}" unless File.lstat(path).file?
  end

  def self.parents(paths)
    paths.flat_map do |p|
      parts = p.split('/')
      (1...parts.size).map { |n| parts.take(n).join('/') }
    end.uniq
  end

  # Preserve native explicit metadata. Never import an mtree directive, path
  # escape, link, content override or duplicate attribute into the root spec.
  def self.metadata(line, path, type)
    words = line.split
    raise 'Invalid metadata path' unless words.shift == './' + path
    fields = {}
    words.each do |word|
      key, value = word.split('=', 2)
      raise 'Invalid metadata attribute' if fields.key?(key) || value.nil?
      valid = case key
      when 'type' then value == type
      when 'uname', 'gname' then value.match?(/\A[A-Za-z_][A-Za-z0-9_-]*\z/)
      when 'mode' then value.match?(/\A0?[0-7]{3,4}\z/)
      when 'size', 'uid', 'gid' then value.match?(/\A[0-9]+\z/)
      when 'time' then value.match?(/\A[0-9]+(?:\.[0-9]+)?\z/)
      when 'flags' then value.match?(/\A[A-Za-z0-9_,+-]+\z/)
      when 'sha256' then value.match?(/\A[0-9a-f]{64}\z/)
      else false
      end
      raise 'Unsupported native DATA metadata' unless valid
      fields[key] = value
    end
    raise 'Incomplete native DATA metadata' unless %w[type uname gname mode].all? { |k| fields.key?(k) }
    fields
  end

  def self.snapshot(cohort, revision)
    raise 'Invalid source revision' unless revision.match?(/\A[0-9a-f]{40}\z/)
    dest = cohort + '/dest'
    %w[usr usr/share usr/share/i18n].each do |p|
      raise 'DATA directory is not a directory' unless File.lstat(dest + '/' + p).directory?
    end
    files = []
    Find.find(dest + '/usr/share/i18n') do |p|
      stat = File.lstat(p)
      next if stat.directory?
      rel = p.delete_prefix(dest + '/')
      raise 'Unsupported DATA path or file type' unless stat.file? && ALLOWED.include?(rel)
      files << rel
    end
    raise 'Required native DATA missing' unless (REQUIRED - files).empty?
    paths = (files + parents(files)).sort
    regular(dest + '/METALOG')
    native = {}
    data_native = {}
    File.foreach(dest + '/METALOG') do |line|
      key = line.split.first
      if key && key.start_with?('./usr/share/i18n/')
        components = key.delete_prefix('./').split('/')
        raise 'Malformed native DATA path' unless components.all? { |p| p.match?(/\A[A-Za-z0-9_%+-][A-Za-z0-9_.%+-]*\z/) }
        data_native[key.delete_prefix('./')] = line.strip
      end
      next unless key && paths.include?(key.delete_prefix('./'))
      native[key.delete_prefix('./')] = line.strip
    end
    data_native.each do |p, line|
      type = line.split.find { |word| word.start_with?('type=') }&.delete_prefix('type=')
      raise 'Unsupported native DATA type' unless %w[file dir].include?(type)
      metadata(line, p, type)
      if type == 'file'
        raise 'Missing or unsupported native installed DATA' unless ALLOWED.include?(p) && files.include?(p)
      end
    end
    entries = paths.map do |p|
      stat = File.lstat(dest + '/' + p)
      type = stat.directory? ? 'dir' : 'file'
      line = native.fetch(p) { raise "Missing native DATA metadata: #{p}" }
      fields = metadata(line, p, type)
      row = { 'path' => p, 'metadata' => line }
      if type == 'file'
        row['sha256'] = Digest::SHA256.file(dest + '/' + p).hexdigest
        raise 'Native metadata size mismatch' if fields['size'] && fields['size'].to_i != stat.size
        raise 'Native metadata hash mismatch' if fields['sha256'] && fields['sha256'] != row['sha256']
      end
      row
    end
    { 'format' => 1, 'source' => revision, 'entries' => entries }
  end

  def self.verified(cohort, revision, receipt)
    path = cohort + '/' + INVENTORY
    regular(path)
    regular(receipt)
    pins = File.readlines(receipt).grep(/^i18n-inventory-sha256=/)
    raise 'DATA receipt hash mismatch' unless pins == ['i18n-inventory-sha256=' + Digest::SHA256.file(path).hexdigest + "\n"]
    stored = parse(File.read(path))
    actual = snapshot(cohort, revision)
    raise 'Native DATA inventory differs' unless stored == actual
    actual
  end

  def self.copy(cohort, root, inventory)
    inventory.fetch('entries').each do |row|
      path = root + '/' + row.fetch('path')
      type = row.key?('sha256') ? 'file' : 'dir'
      fields = metadata(row.fetch('metadata'), row.fetch('path'), type)
      if type == 'dir'
        FileUtils.mkdir_p(path)
      else
        raise 'Staged DATA already exists' if File.exist?(path) || File.symlink?(path)
        FileUtils.copy_file(cohort + '/dest/' + row.fetch('path'), path)
      end
      File.chmod(fields.fetch('mode').to_i(8), path) if type == 'file'
    end
    # A native directory may be read-only. Populate children before its chmod.
    inventory.fetch('entries').reverse_each do |row|
      next if row.key?('sha256')
      fields = metadata(row.fetch('metadata'), row.fetch('path'), 'dir')
      File.chmod(fields.fetch('mode').to_i(8), root + '/' + row.fetch('path'))
    end
  end

  def self.check_copy(root, inventory)
    expected = inventory.fetch('entries').select { |row| row.key?('sha256') }.map { |row| row.fetch('path') }.sort
    actual = []
    Find.find(root + '/usr/share/i18n') do |path|
      stat = File.lstat(path)
      next if stat.directory?
      raise 'Invalid staged DATA file type' unless stat.file?
      actual << path.delete_prefix(root + '/')
    end
    raise 'Staged DATA inventory differs' unless actual.sort == expected
    inventory.fetch('entries').each do |row|
      path = root + '/' + row.fetch('path')
      stat = File.lstat(path)
      type = row.key?('sha256') ? 'file' : 'dir'
      fields = metadata(row.fetch('metadata'), row.fetch('path'), type)
      raise 'Changed staged DATA type/mode' unless (type == 'file' ? stat.file? : stat.directory?) &&
        (stat.mode & 0o7777) == fields.fetch('mode').to_i(8)
      raise 'Changed staged DATA bytes' if type == 'file' && Digest::SHA256.file(path).hexdigest != row.fetch('sha256')
    end
  end
end

if $PROGRAM_NAME == __FILE__
  begin
    action, cohort, revision, receipt, root = ARGV
    raise 'Usage: cix-p1-i18n.rb record COHORT REVISION | verify COHORT REVISION RECEIPT | copy/check-copy COHORT REVISION RECEIPT ROOT' unless
      (action == 'record' && ARGV.size == 3) || (action == 'verify' && ARGV.size == 4) ||
      (%w[copy check-copy].include?(action) && ARGV.size == 5)
    if action == 'record'
      data = CixI18n.snapshot(cohort, revision)
      path = cohort + '/' + CixI18n::INVENTORY
      File.open(path, File::WRONLY | File::CREAT | File::EXCL, 0o644) { |f| f.write(JSON.pretty_generate(data) + "\n") }
    else
      data = CixI18n.verified(cohort, revision, receipt)
      CixI18n.copy(cohort, root, data) if action == 'copy'
      CixI18n.check_copy(root, data) if action == 'check-copy'
      data.fetch('entries').each { |row| puts row.fetch('metadata') } if action == 'verify'
    end
  rescue StandardError => e
    warn 'Native i18n DATA: ' + e.message
    exit 1
  end
end
