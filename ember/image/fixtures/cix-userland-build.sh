#!@SHELL@
TEST_BUILD_ARGS="$*"
while [ "$#" -gt 0 ]; do
  case "$1" in -D) shift; dest=$1;; -w) shift; wrapper=$1;; esac
  shift
done
export dest
printf '%s\n' "$TEST_BUILD_ARGS" > "$dest/../build-flags"
printf '%s\n' "$dest" > "$wrapper.dest"
cat > "$wrapper" <<'WRAPPER'
#!@SHELL@
dest=$(cat "$0.dest")
printf '%s\n' "$*" >> "$dest/../events"
printf '%s\n' "$CPPFLAGS" > "$dest/../header-flags"
case "$*" in *"/sbin/mknod "*"$TEST_FAIL_MKNOD_STAGE")
  test -z "$TEST_FAIL_MKNOD_STAGE" || exit 17;; esac
case "$*" in *"$TEST_FAIL_COMPONENT"*)
  test -z "$TEST_FAIL_COMPONENT" || exit 17;; esac
case "$*" in *'/share/i18n/esdb '*)
  selected=0
  for arg do [ "$arg" != 'SUBDIR=ISO646 UTF' ] || selected=1; done
  [ "$selected" = 1 ] || exit 71;;
esac
case "$*" in *'/share/i18n/'*' install')
  files=
  case "$*" in
  *'/iconv '*) files='iconv/iconv.dir';;
  *'/esdb '*) files='esdb/esdb.dir esdb/esdb.alias esdb/esdb.dir.db esdb/esdb.alias.db esdb/ISO646/ISO646-US.esdb esdb/UTF/UTF-8.esdb';;
  *'/csmapper '*) files='csmapper/mapper.dir csmapper/mapper.dir.db csmapper/charset.pivot csmapper/charset.pivot.pvdb csmapper/ISO646/ISO646-US%UCS.646';;
  esac
  for file in $files; do
    test "$file" != "$TEST_OMIT_DATA" || continue
    rel=usr/share/i18n/$file
    mkdir -p "$dest/$(dirname "$rel")"
    dir=$(dirname "$rel")
    while [ "$dir" != . ]; do
      printf './%s type=dir uname=root gname=wheel mode=0755\n' "$dir" >> "$dest/METALOG"
      dir=$(dirname "$dir")
    done
    printf 'synthetic native recipe output\n' > "$dest/$rel"
    printf './%s type=file uname=root gname=wheel mode=0444 size=%s\n' \
      "$rel" "$(wc -c < "$dest/$rel" | tr -d ' ')" >> "$dest/METALOG"
  done;;
esac
case "$*" in *'/share/i18n/csmapper '*' install')
  case "$TEST_BAD_DATA" in
  metadata) printf './usr/share/i18n/iconv/iconv.dir type=file uname=root gname=wheel mode=0444 content=/etc/passwd\n' >> "$dest/METALOG";;
  path) printf './usr/share/i18n/../escape type=file uname=root gname=wheel mode=0444\n' >> "$dest/METALOG";;
  unsupported) printf 'unsupported\n' > "$dest/usr/share/i18n/esdb/unreviewed.esdb";;
  esac;;
esac
case "$*" in
  *do-distrib-dirs*) mkdir -p "$dest/bin" "$dest/sbin" "$dest/usr/bin" \
    "$dest/usr/sbin" "$dest/usr/libexec";;
  *"/sbin/mknod install")
    if [ "$TEST_OMIT_BINARY" != sbin/mknod ]; then
      printf 'candidate\n' > "$dest/sbin/mknod"
    fi;;
  *install*)
    for file in bin/sh bin/stty usr/sbin/usbdevs sbin/init sbin/ifconfig sbin/dhcpcd sbin/fsck \
      sbin/fsck_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl \
      usr/bin/stat usr/bin/wall usr/sbin/wpa_supplicant usr/sbin/wpa_cli \
      usr/sbin/powerd usr/libexec/getty sbin/mount_ptyfs usr/sbin/sshd \
      usr/libexec/sshd-session usr/libexec/sshd-auth usr/bin/ssh-keygen \
      usr/bin/passwd usr/sbin/pwd_mkdb usr/sbin/envstat sbin/nvmectl sbin/wdogctl \
      usr/sbin/acpidump; do
      test "$file" = "$TEST_OMIT_BINARY" && continue
      mkdir -p "$dest/$(dirname "$file")"
      printf 'candidate\n' > "$dest/$file"
    done;;
esac
WRAPPER
chmod 755 "$wrapper"
