#!/bin/sh
# Read-only acceptance of the existing VideoCore property consumers.
# This exercises successful requests; it never injects mailbox/DMA faults.
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
umask 077

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /absolute/new-output-directory" >&2
    exit 2
fi
out=$1
case "$out" in
/*) ;;
*) echo 'output directory must be absolute' >&2; exit 2 ;;
esac
[ "$(uname -s)" = NetBSD ]
[ "$(uname -p)" = aarch64 ]
mkdir "$out"
uname -a > "$out/uname"
sysctl kern.boottime > "$out/boottime"
dmesg > "$out/dmesg-before"
min=$(sysctl -n machdep.cpu.frequency.min)
max=$(sysctl -n machdep.cpu.frequency.max)
case "$min:$max" in
*[!0-9:]*|:*|*:) echo 'invalid firmware frequency bounds' >&2; exit 1 ;;
esac
[ "$min" -gt 0 ]
[ "$max" -ge "$min" ]
date -u +%Y-%m-%dT%H:%M:%SZ > "$out/start"
start=$(date +%s)
round=1
while [ "$round" -le 32 ]; do
    pids=
    worker=1
    while [ "$worker" -le 4 ]; do
        sysctl -n machdep.cpu.frequency.current \
            > "$out/clock-$round-$worker" \
            2> "$out/error-$round-$worker" &
        pids="$pids $!"
        worker=$((worker + 1))
    done
    result=0
    for pid in $pids; do
        wait "$pid" || result=1
    done
    [ "$result" -eq 0 ]
    envstat -d vcmbox0 > "$out/sensors-$round"
    awk '/temperature:/ {
        if ($2 !~ /^[0-9]+\.[0-9]+$/ || $2 < 0 || $2 >= $3) exit 1
        t++
    }
    /under-voltage|throttled/ { if ($NF != "FALSE") exit 1; f++ }
    END { if (t != 1 || f != 4) exit 1 }' "$out/sensors-$round"
    round=$((round + 1))
    sleep 1
done
awk -v min="$min" -v max="$max" '{
    if (NF != 1 || $1 !~ /^[0-9]+$/ || $1 < min || $1 > max) exit 1
    n++
}
END {
    if (n != 128) exit 1
    print "PASS: " n " concurrent clock reads within " min ".." max " MHz"
}' "$out"/clock-* > "$out/result"
[ "$(cat "$out"/error-* | wc -c | tr -d ' ')" = 0 ]
sysctl machdep.cpu.frequency.current > "$out/final-clock"
dmesg > "$out/dmesg-after"
# A new unrelated kernel diagnostic also requires investigation, not a PASS.
cmp "$out/dmesg-before" "$out/dmesg-after"
date -u +%Y-%m-%dT%H:%M:%SZ > "$out/end"
printf 'Duration: %s seconds\n' "$(( $(date +%s) - start ))" >> "$out/result"
printf '%s\n' 'PASS: 32 valid sensor samples, no new kernel diagnostics, final property read succeeds' >> "$out/result"
cat "$out/result"
