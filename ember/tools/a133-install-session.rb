#!/usr/bin/env ruby
# Origin: EmberBSD - private crash-aware coordination of guarded write stages.
require 'securerandom'
require_relative 'a133-install-bundle'
require_relative 'a133-usb-transfer'

module A133Install
  ORDER=%w[root boot resources].freeze
  STATES=%w[pending checking writing verified failed].freeze
  class Invalid < StandardError
    attr_reader :report
    def initialize(reason,report=nil)
      super(reason)
      @report=report || {installation_ready: false}
    end
  end
  module_function

  def exact?(value,keys)
    value.is_a?(Hash) && value.keys.all? { |key| key.is_a?(String) } && value.keys.sort==keys.sort
  end
  def sha?(value)
    value.is_a?(String) && value.match?(/\A[0-9a-f]{64}\z/)
  end
  def canonical(value)
    case value
    when Hash then value.keys.sort.each_with_object({}) { |key,out| out[key]=canonical(value[key]) }
    when Array then value.map { |entry| canonical(entry) }
    else value
    end
  end
  def digest(value)
    Digest::SHA256.hexdigest(JSON.generate(canonical(value)))
  end
  def bundle_digest(receipt)
    copy=JSON.parse(JSON.generate(receipt))
    copy['artifacts'].sort_by! { |row| row['role'] }
    digest(copy)
  end
  def context_valid!(context)
    valid=exact?(context,%w[schema board serial cid source_commit backup_sha256 recovery_sha256 protected_env_sha256 artifacts]) &&
      context['schema'].is_a?(Integer) && context['schema']==1 && context['board']=='ys-m33-a133' &&
      context['serial'].is_a?(String) && context['serial'].bytesize.between?(1,128) &&
      context['serial'].match?(/\A[a-zA-Z0-9._-]+\z/) && context['cid'].is_a?(String) &&
      context['cid'].match?(/\A[0-9a-f]{32}\z/) && context['source_commit'].is_a?(String) &&
      context['source_commit'].match?(/\A[0-9a-f]{40}\z/) &&
      %w[backup_sha256 recovery_sha256 protected_env_sha256].all? { |key| sha?(context[key]) }
    rows=valid ? context['artifacts'] : nil
    valid &&= rows.is_a?(Array) && rows.size==3 && rows.all? do |row|
      exact?(row,%w[role bytes sha256]) && ORDER.include?(row['role']) && sha?(row['sha256']) &&
        row['bytes'].is_a?(Integer) && row['bytes']>0 && row['bytes']%512==0 &&
        row['bytes']<=A133Usb::Client::ROLES.fetch(row['role'])[1] &&
        (row['role']=='root' || row['bytes']==33554432)
    end
    valid &&= rows.map { |row| row['role'] }.sort==ORDER.sort
    raise Invalid,'session_context_invalid' unless valid
    true
  end

  class Store
    def self.secure_file!(stat)
      raise Invalid,'session_file_unsafe' unless stat.file? && stat.uid==Process.uid &&
        stat.mode&07777==0600 && stat.nlink==1
    end
    def self.open(directory,context)
      A133Install.context_valid!(context) if context
      path=File.expand_path(directory)
      unless File.exist?(path) || File.symlink?(path)
        raise Invalid,'session_missing' unless context
        Dir.mkdir(path,0700)
      end
      stat=File.lstat(path)
      raise Invalid,'session_directory_unsafe' unless stat.directory?
      raise Invalid,'session_permissions' unless stat.uid==Process.uid && stat.mode&07777==0700
      lock=File.join(path,'.lock')
      secure_file!(File.lstat(lock)) if File.exist?(lock) || File.symlink?(lock)
      raise Invalid,'session_missing' if !context && !File.exist?(lock)
      flags=File::RDWR|File::NOFOLLOW|File::NONBLOCK
      flags|=File::CREAT if context
      File.open(path,File::RDONLY|File::NOFOLLOW) do |directory_file|
        directory_file.close_on_exec=true
        File.open(lock,flags,0600) do |file|
          secure_file!(file.stat)
          file.close_on_exec=true
          raise Invalid,'session_busy' unless file.flock(File::LOCK_EX|File::LOCK_NB)
          store=new(path,directory_file,file)
          store.__send__(:locked,context) { |value| yield value }
        end
      end
    rescue SystemCallError,IOError
      raise Invalid,'session_io_failed'
    end

    def initialize(directory,directory_file,lock_file)
      @directory=directory
      @path=File.join(directory,'state.json')
      @directory_file,@lock_file=directory_file,lock_file
    end
    def locked(context)
      @owner_pid=Process.pid
      @owner_thread=Thread.current
      begin
        assert_locked!
        load_or_create(context)
        yield self
      ensure
        @owner_pid=nil
        @owner_thread=nil
      end
    end
    def assert_locked!
      raise Invalid,'session_not_locked' unless @owner_pid==Process.pid && @owner_thread==Thread.current
      current=File.lstat(@directory)
      held=@directory_file.stat
      raise Invalid,'session_scope_changed' unless [current,held].all? do |stat|
        stat.directory? && stat.uid==Process.uid && stat.mode&07777==0700
      end && current.dev==held.dev && current.ino==held.ino
      current=File.lstat(File.join(@directory,'.lock'))
      held=@lock_file.stat
      self.class.secure_file!(current)
      self.class.secure_file!(held)
      raise Invalid,'session_scope_changed' unless current.dev==held.dev && current.ino==held.ino
    end
    def valid_state!(data)
      valid=A133Install.exact?(data,%w[schema context session_id revision roles checksum]) &&
        data['schema'].is_a?(Integer) && data['schema']==1 && data['session_id'].is_a?(String) &&
        data['session_id'].match?(/\A[0-9a-f]{32}\z/) && data['revision'].is_a?(Integer) &&
        data['revision'].between?(0,1000000) && A133Install.exact?(data['roles'],ORDER) &&
        A133Install.sha?(data['checksum'])
      raise Invalid,'session_state_invalid' unless valid
      begin
        A133Install.context_valid!(data['context'])
      rescue Invalid
        raise Invalid,'session_state_invalid'
      end
      raise Invalid,'session_state_invalid' unless data['roles'].values.all? do |row|
        A133Install.exact?(row,%w[state reason]) && STATES.include?(row['state']) &&
          (row['reason'].nil? || (row['reason'].is_a?(String) && row['reason'].match?(/\A[a-z0-9_]{1,64}\z/)))
      end
      unsigned=data.reject { |key,_| key=='checksum' }
      raise Invalid,'session_state_invalid' unless A133Install.digest(unsigned)==data['checksum']
      true
    end
    def read
      self.class.secure_file!(File.lstat(@path))
      bytes=File.open(@path,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |file|
        before=file.stat
        self.class.secure_file!(before)
        raise Invalid,'session_state_invalid' if before.size>65536
        text=file.read(65537) || ''
        after=file.stat
        raise Invalid,'session_state_invalid' unless text.bytesize<=65536 &&
          [:dev,:ino,:size,:mtime,:ctime].all? { |key| before.public_send(key)==after.public_send(key) }
        text
      end
      data=JSON.parse(bytes,object_class: BundleObject,allow_duplicate_key: false)
      valid_state!(data)
      data
    rescue BundleError,JSON::ParserError,ArgumentError,EncodingError
      raise Invalid,'session_state_invalid'
    end
    def publish(data)
      assert_locked!
      data=data.reject { |key,_| key=='checksum' }
      data['checksum']=A133Install.digest(data)
      valid_state!(data)
      text=JSON.generate(data)
      raise Invalid,'session_state_invalid' if text.bytesize>65536
      self.class.secure_file!(File.lstat(@path)) if File.exist?(@path) || File.symlink?(@path)
      temp=File.join(@directory,"state.#{SecureRandom.hex(16)}.tmp")
      owned=nil
      File.open(temp,File::WRONLY|File::CREAT|File::EXCL|File::NOFOLLOW,0600) do |file|
        owned=file.stat
        file.write(text)
        file.flush
        file.fsync
      end
      assert_locked!
      File.rename(temp,@path)
      # Publication is already visible even if the directory fsync fails.
      @data=data
      @directory_file.fsync
    ensure
      if owned && temp
        begin
          current=File.lstat(temp)
          File.unlink(temp) if current.dev==owned.dev && current.ino==owned.ino
        rescue Errno::ENOENT
          # Successfully published or already removed; never delete another inode.
        end
      end
    end
    def load_or_create(context)
      if File.exist?(@path) || File.symlink?(@path)
        @data=read
        raise Invalid,'session_context_changed' if context && canonical_context(context)!=canonical_context(@data['context'])
      else
        raise Invalid,'session_missing' unless context
        publish({'schema'=>1,'context'=>JSON.parse(JSON.generate(context)),'session_id'=>SecureRandom.hex(16),'revision'=>0,
          'roles'=>ORDER.each_with_object({}) { |role,out| out[role]={'state'=>'pending','reason'=>nil} }})
      end
    end
    def canonical_context(context)
      copy=JSON.parse(JSON.generate(context))
      copy['artifacts'].sort_by! { |row| row['role'] }
      A133Install.canonical(copy)
    end
    def record(role,state,reason=nil)
      assert_locked!
      raise Invalid,'session_state_invalid' unless ORDER.include?(role) && STATES.include?(state) &&
        (reason.nil? || (reason.is_a?(String) && reason.match?(/\A[a-z0-9_]{1,64}\z/)))
      data=JSON.parse(JSON.generate(@data))
      data['revision']+=1
      data['roles'][role]={'state'=>state,'reason'=>reason}
      publish(data)
    rescue SystemCallError,IOError
      raise Invalid,'session_io_failed'
    end
    def report
      {status: 'session_progress',session_id: @data['session_id'],revision: @data['revision'],
       roles: JSON.parse(JSON.generate(@data['roles'])),installation_ready: false}
    end
    private :read,:publish,:valid_state!,:canonical_context,:locked,:assert_locked!,:load_or_create
    private_class_method :new
  end

  def run(directory:,manifest:,adb:,serial:,cid:,backup:,protected_env:,timeout: 600,expected_bundle_sha256:nil,additional_guard:nil)
    store=nil
    active=nil
    written=0
    attempted=false
    unless expected_bundle_sha256.nil?
      raise Invalid,'session_bundle_changed' unless expected_bundle_sha256.is_a?(String) &&
        expected_bundle_sha256.ascii_only? && sha?(expected_bundle_sha256)
      expected_bundle_sha256=expected_bundle_sha256.dup.freeze
    end
    verified=A133Bundle.verify(manifest)
    unless expected_bundle_sha256.nil?
      raise Invalid,'session_bundle_changed' unless sha?(expected_bundle_sha256) &&
        bundle_digest(verified[:receipt])==expected_bundle_sha256
    end
    raise Invalid,'invalid_backup_receipt' unless backup.is_a?(Hash) && backup['cid']==cid &&
      backup['status']=='backup_integrity_verified' && backup['bytes']==A133Backup::BYTES &&
      %w[gpt_headers_crc gpt_arrays_crc partition_layout_verified].all? { |key| backup[key]==true } &&
      sha?(backup['uncompressed_sha256']) && backup['partition_sha256'].is_a?(Hash) &&
      backup['partition_sha256'].keys.sort==%w[boot bootloader env recovery] &&
      backup['partition_sha256'].values.all? { |value| sha?(value) }
    raise Invalid,'invalid_protected_environment' unless protected_env.is_a?(String) && protected_env.bytesize==131072
    backup=JSON.parse(JSON.generate(backup))
    protected_env=protected_env.dup.freeze
    serial=serial.dup.freeze if serial.is_a?(String)
    cid=cid.dup.freeze if cid.is_a?(String)
    context={'schema'=>1,'board'=>'ys-m33-a133','serial'=>serial,'cid'=>cid,
      'source_commit'=>verified[:receipt][:source_commit],'backup_sha256'=>backup['uncompressed_sha256'],
      'recovery_sha256'=>backup['partition_sha256']['recovery'],
      'protected_env_sha256'=>Digest::SHA256.hexdigest(protected_env),
      'artifacts'=>verified[:receipt][:artifacts].map do |row|
        {'role'=>row[:role],'bytes'=>row[:bytes],'sha256'=>row[:sha256]}
      end}
    Store.open(directory,context) do |locked|
      store=locked
      begin
        client=A133Usb::Client.new(adb: adb,serial: serial,cid: cid,timeout: timeout,additional_guard:additional_guard)
        ORDER.each do |role|
          active=role
          row=verified[:receipt][:artifacts].find { |artifact| artifact[:role]==role }
          params={role: role,bytes: row[:bytes],sha256: row[:sha256],backup: backup,protected_env: protected_env}
          store.record(role,'checking')
          mismatch=false
          begin
            client.verify_installed(**params)
          rescue A133Usb::Invalid=>error
            raise unless error.message=='usb_readback_mismatch'
            mismatch=true
          end
          if mismatch
            store.record(role,'writing')
            begin
              client.write_verified(**params.merge(path: verified[:paths].fetch(role)))
              attempted=true
              written+=1
            rescue A133Usb::Invalid=>error
              attempted ||= !!error.write_attempted
              raise
            end
          end
          store.record(role,'verified')
        end
        store.report.merge(status: 'write_stage_verified',writes_performed: written)
      rescue StandardError=>error
        reason=reason_for(error)
        if active
          begin
            store.record(active,'failed',reason)
          rescue Invalid,SystemCallError,IOError
            # Keep the original error and conservative prior hint on persistence failure.
          end
        end
        report=store.report.merge(status: 'write_stage_stopped',writes_performed: written,write_attempted: attempted)
        raise Invalid.new(reason,report)
      end
    end
  rescue StandardError=>error
    raise if error.is_a?(Invalid) && error.report[:status]=='write_stage_stopped'
    raise Invalid.new(reason_for(error),{status: 'write_stage_stopped',installation_ready: false,
      writes_performed: written,write_attempted: attempted})
  end
  def reason_for(error)
    reason=error.is_a?(Invalid) || error.is_a?(A133Usb::Invalid) || error.is_a?(BundleError) ? error.message : 'install_operation_failed'
    reason.match?(/\A[a-z0-9_]{1,64}\z/) ? reason : 'install_operation_failed'
  end
end

if $PROGRAM_NAME==__FILE__
  begin
    raise A133Install::Invalid,'invalid_arguments' unless ARGV.size==1 && !ARGV.first.start_with?('-')
    A133Install::Store.open(ARGV.first,nil) { |store| puts JSON.pretty_generate(store.report) }
  rescue A133Install::Invalid,ArgumentError,SystemCallError,IOError=>error
    reason=error.is_a?(A133Install::Invalid) ? error.message : 'session_io_failed'
    puts JSON.pretty_generate({status: 'session_inspection_stopped',reason: reason,installation_ready: false})
    exit 1
  end
end
