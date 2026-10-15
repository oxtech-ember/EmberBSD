#!/usr/bin/env ruby
# Origin: EmberBSD - locked private journal for monotonic vendor unlock intent.
require 'securerandom'
require_relative 'a133-unlock-env'
require_relative 'a133-install-bundle'

module A133Unlock
  class Journal
    HASHES=%w[backup gpt bootloader boot recovery original_env unlock_env mutable].map { |key| key+'_sha256' }.freeze
    PHASES=%w[checking arming armed rebooting restoring verified failed].freeze
    def self.exact?(value,keys)
      value.is_a?(Hash) && value.keys.all? { |key| key.is_a?(String) } && value.keys.sort==keys.sort
    end
    def self.context!(value)
      valid=exact?(value,%w[serial cid]+HASHES) && value.values.all? { |item| item.is_a?(String) && item.ascii_only? }
      valid &&= value['serial'].match?(/\A[a-zA-Z0-9._-]{1,128}\z/) && value['cid'].match?(/\A[0-9a-f]{32}\z/) &&
        HASHES.all? { |key| value[key].match?(/\A[0-9a-f]{64}\z/) }
      raise Invalid,'unlock_journal_context_invalid',cause:nil unless valid
    end
    def self.canonical(value)
      value.is_a?(Hash) ? value.keys.sort.to_h { |key| [key,canonical(value[key])] } : value
    end
    def self.digest(value)
      Digest::SHA256.hexdigest(JSON.generate(canonical(value)))
    end
    def self.secure!(stat)
      raise Invalid,'unlock_journal_file_unsafe',cause:nil unless stat.file? && stat.uid==Process.uid && stat.mode&07777==0600 && stat.nlink==1
    end
    def self.directory_secure!(stat)
      raise Invalid,'unlock_journal_directory_unsafe',cause:nil unless stat.directory? && stat.uid==Process.uid && stat.mode&07777==0700
    end
    def self.open(directory,context)
      raise Invalid,'unlock_journal_directory_unsafe',cause:nil unless directory.is_a?(String) && !directory.empty? &&
        directory.encoding.ascii_compatible? && directory.valid_encoding? && !directory.b.include?("\0")
      if context
        context!(context)
        context=context.to_h { |key,value| [key,value.dup.freeze] }.freeze
        context!(context)
      end
      path=File.expand_path(directory)
      unless File.exist?(path) || File.symlink?(path)
        raise Invalid,'unlock_journal_missing',cause:nil unless context
        Dir.mkdir(path,0700)
      end
      directory_secure!(File.lstat(path))
      lock=File.join(path,'.lock')
      exists=File.exist?(lock) || File.symlink?(lock)
      secure!(File.lstat(lock)) if exists
      raise Invalid,'unlock_journal_missing',cause:nil unless context || exists
      flags=File::RDWR|File::NOFOLLOW|File::NONBLOCK
      flags|=File::CREAT if context
      File.open(path,File::RDONLY|File::NOFOLLOW) do |directory_file|
        directory_secure!(directory_file.stat)
        directory_file.close_on_exec=true
        File.open(lock,flags,0600) do |file|
          secure!(file.stat)
          file.close_on_exec=true
          raise Invalid,'unlock_journal_busy',cause:nil unless file.flock(File::LOCK_EX|File::LOCK_NB)
          new(path,directory_file,file).__send__(:locked,context) { |store| yield store }
        end
      end
    rescue SystemCallError,IOError
      raise Invalid,'unlock_journal_io_failed',cause:nil
    end
    def initialize(directory,directory_file,lock_file)
      @directory=directory
      @path=File.join(directory,'state.json')
      @directory_file,@lock_file=directory_file,lock_file
    end
    def locked(context)
      @pid,@thread=Process.pid,Thread.current
      begin
        lock!
        if File.exist?(@path) || File.symlink?(@path)
          @data=read
          raise Invalid,'unlock_journal_context_changed',cause:nil if context && @data['context']!=context
        else
          raise Invalid,'unlock_journal_missing',cause:nil unless context
          publish({'schema'=>1,'context'=>context,'phase'=>'checking','revision'=>0,
            'possible_write'=>false,'possible_reboot'=>false,'hardware_bytes'=>nil,'reason'=>nil})
        end
        yield self
      ensure
        @pid=@thread=nil
      end
    end
    def lock!
      raise Invalid,'unlock_journal_not_locked',cause:nil unless @pid==Process.pid && @thread==Thread.current
      [[@directory,@directory_file,:directory_secure!],
        [File.join(@directory,'.lock'),@lock_file,:secure!]].each do |path,file,check|
        current=File.lstat(path)
        held=file.stat
        self.class.public_send(check,current)
        self.class.public_send(check,held)
        raise Invalid,'unlock_journal_scope_changed',cause:nil unless current.dev==held.dev && current.ino==held.ino
      end
    end
    def valid!(data)
      valid=self.class.exact?(data,%w[schema context phase revision possible_write possible_reboot hardware_bytes reason checksum]) &&
        data['schema']==1 && data['schema'].is_a?(Integer) && PHASES.include?(data['phase']) &&
        data['revision'].is_a?(Integer) && data['revision'].between?(0,1000000) &&
        %w[possible_write possible_reboot].all? { |key| [true,false].include?(data[key]) } &&
        (data['hardware_bytes'].nil? || (data['hardware_bytes'].is_a?(Integer) && data['hardware_bytes'].between?(512,33554432) && data['hardware_bytes']%512==0)) &&
        (data['reason'].nil? || (data['reason'].is_a?(String) && data['reason'].ascii_only? && data['reason'].match?(/\A[a-z0-9_]{1,64}\z/)))
      raise Invalid,'unlock_journal_state_invalid',cause:nil unless valid
      begin
        self.class.context!(data['context'])
      rescue Invalid
        raise Invalid,'unlock_journal_state_invalid',cause:nil
      end
      raise Invalid,'unlock_journal_state_invalid',cause:nil unless data['checksum']==self.class.digest(data.reject { |key,_| key=='checksum' })
    end
    def read
      self.class.secure!(File.lstat(@path))
      text=File.open(@path,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |file|
        before=file.stat; self.class.secure!(before)
        raise Invalid,'unlock_journal_state_invalid',cause:nil if before.size>65536
        value=file.read(65537) || ''
        after=file.stat
        raise Invalid,'unlock_journal_state_invalid',cause:nil unless value.bytesize<=65536 &&
          [:dev,:ino,:size,:mtime,:ctime].all? { |key| before.public_send(key)==after.public_send(key) }
        value
      end
      data=JSON.parse(text,object_class:BundleObject,allow_duplicate_key:false)
      valid!(data)
      data
    rescue BundleError,JSON::ParserError,ArgumentError,EncodingError
      raise Invalid,'unlock_journal_state_invalid',cause:nil
    end
    def publish(data)
      lock!
      data=data.reject { |key,_| key=='checksum' }
      data['checksum']=self.class.digest(data)
      valid!(data)
      text=JSON.generate(data)
      raise Invalid,'unlock_journal_state_invalid',cause:nil if text.bytesize>65536
      self.class.secure!(File.lstat(@path)) if File.exist?(@path) || File.symlink?(@path)
      temp=File.join(@directory,"state.#{SecureRandom.hex(16)}.tmp")
      owned=nil
      begin
        File.open(temp,File::WRONLY|File::CREAT|File::EXCL|File::NOFOLLOW,0600) do |file|
          owned=file.stat
          file.write(text); file.flush; file.fsync
        end
        lock!
        File.rename(temp,@path)
        # Rename already made this intent visible; a later fsync failure must
        # not let failed-result handling publish the previous, weaker flags.
        @data=data
        @directory_file.fsync
      ensure
        if owned
          begin
            stat=File.lstat(temp)
            File.unlink(temp) if stat.dev==owned.dev && stat.ino==owned.ino
          rescue Errno::ENOENT
            # A published or removed inode requires no cleanup.
          rescue SystemCallError,IOError
            raise Invalid,'unlock_journal_io_failed',cause:nil
          end
        end
      end
    rescue SystemCallError,IOError
      raise Invalid,'unlock_journal_io_failed',cause:nil
    end
    def record(phase,write:false,reboot:false,hardware_bytes:nil,reason:nil)
      lock!
      raise Invalid,'unlock_journal_state_invalid',cause:nil unless [true,false].include?(write) && [true,false].include?(reboot)
      data=snapshot
      data['phase']=phase; data['revision']+=1
      data['possible_write']||=write; data['possible_reboot']||=reboot
      if hardware_bytes
        raise Invalid,'unlock_hardware_changed',cause:nil if data['hardware_bytes'] && data['hardware_bytes']!=hardware_bytes
        data['hardware_bytes']=hardware_bytes
      end
      data['reason']=reason
      publish(data)
    end
    def snapshot
      lock!
      JSON.parse(JSON.generate(@data))
    end
    def report
      snapshot.reject { |key,_| %w[context checksum schema].include?(key) }.transform_keys(&:to_sym).merge(installation_ready:false)
    end
    private :locked,:lock!,:valid!,:read,:publish
    private_class_method :new
  end
end
