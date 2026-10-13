#!/bin/sh
# Origin: EmberBSD (AI-assisted), package host fixtures with native target LLVM.
set -eu
[ "$#" -ge 3 ] && [ "$1" = -o ] || {
    echo "Usage: CTF_DWP_SSH_TARGET=host $0 -o OUTPUT INPUT.dwo ..." >&2
    exit 2
}
: "${CTF_DWP_SSH_TARGET:?Set an SSH destination for the target LLVM installation}"
output=$2
shift 2
ssh_run()
{
    if [ -n "${CTF_DWP_KNOWN_HOSTS:-}" ]; then
        ssh -o BatchMode=yes -o "UserKnownHostsFile=$CTF_DWP_KNOWN_HOSTS" "$CTF_DWP_SSH_TARGET" "$@"
    else
        ssh -o BatchMode=yes "$CTF_DWP_SSH_TARGET" "$@"
    fi
}
copy()
{
    if [ -n "${CTF_DWP_KNOWN_HOSTS:-}" ]; then
        scp -q -o BatchMode=yes -o "UserKnownHostsFile=$CTF_DWP_KNOWN_HOSTS" "$@"
    else
        scp -q -o BatchMode=yes "$@"
    fi
}
remote=$(ssh_run 'umask 077; mktemp -d /tmp/ember-ctf-dwp.XXXXXXXX')
case $remote in /tmp/ember-ctf-dwp.*) ;; *) exit 1 ;; esac
# The generated basename must be safe to quote in the remote shell.
case ${remote#/tmp/ember-ctf-dwp.} in *[!a-zA-Z0-9]*|'') exit 1 ;; esac
trap 'ssh_run "rm -rf -- $remote" >/dev/null 2>&1 || :' EXIT HUP INT TERM
index=0
for input do
    copy "$input" "$CTF_DWP_SSH_TARGET:$remote/input-$index.dwo"
    index=$((index + 1))
done
ssh_run "/usr/pkg/bin/llvm-dwp -o $remote/output.dwp $remote/input-*.dwo"
copy "$CTF_DWP_SSH_TARGET:$remote/output.dwp" "$output"
