#!/usr/bin/env ruby
# Origin: EmberBSD - bounded Linux mount inventory stream and task traversal.
module LinuxMountInventory
  class Invalid < StandardError; end

  # Pin each proc directory. Ignore a failed read only after its task has exited.
  # A live inaccessible task fails the inventory. This is a sampled inventory.
  COMMAND = <<~'SH'.strip.freeze
    for p in /proc/[0-9]*; do
      (
        if ! cd "$p" 2>/dev/null; then
          [ ! -e "$p" ] && exit 0
          exit 1
        fi
        if ! [ -r task ] || ! [ -x task ]; then
          [ -x . ] && [ ! -e stat ] && exit 0
          exit 1
        fi
        for t in task/[0-9]*; do
          [ -e "$t" ] || continue
          (
            if ! cd "$t" 2>/dev/null; then
              [ ! -e "$t" ] && exit 0
              exit 1
            fi
            if ! cat mountinfo 2>/dev/null; then
              [ -x . ] && [ ! -e stat ] || exit 1
            fi
          ) || exit 1
        done
      ) || exit 1
    done
  SH

  class Reader
    MAX_BYTES = 64*1024*1024
    MAX_LINE = 65536
    MAX_DEVICES = 4096

    def initialize
      @bytes=0
      @pending=''.b
      @ids={}
    end

    def feed(chunk)
      @bytes+=chunk.bytesize
      raise Invalid,'mount_inventory_too_large' if @bytes>MAX_BYTES
      @pending<<chunk
      offset=0
      while (last=@pending.index("\n",offset))
        row(@pending.byteslice(offset,last-offset))
        offset=last+1
      end
      @pending=@pending.byteslice(offset,@pending.bytesize-offset)
      raise Invalid,'mount_inventory_too_large' if @pending.bytesize>MAX_LINE
    end

    def finish
      row(@pending) unless @pending.empty?
      @pending=''.b
      raise Invalid,'mount_inventory_invalid' if @ids.empty?
      @ids.keys.freeze
    end

    private

    def row(line)
      raise Invalid,'mount_inventory_too_large' if line.bytesize>MAX_LINE
      fields=line.split
      raise Invalid,'mount_inventory_invalid' unless fields.size>=10 &&
        fields[2].match?(/\A[0-9]{1,10}:[0-9]{1,10}\z/) && (fields.index('-') || 0)>=6
      @ids[fields[2].freeze]=true
      raise Invalid,'mount_inventory_too_large' if @ids.size>MAX_DEVICES
    end
  end
end
