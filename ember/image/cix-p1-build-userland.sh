#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Original EmberBSD reduced C userland build profile, AI-assisted.
# This produces a development cohort, not a complete base set or SD image.
set -eu
export LC_ALL=C
here=$(CDPATH= cd -- "${0%/*}" && pwd)
run()
{
    name=$1; shift
    if [ "$1" = "$wrapper" ]; then shift; set -- "$HOST_SH" "$wrapper" "$@"; fi
    if ! "$@" > "$out/logs/$name.log" 2>&1; then
        echo "Build failed: $out/logs/$name.log" >&2
        return 1
    fi
}
link_if_same()
{
    target=$1 link=$2
    run "link-$(printf '%s' "$link" | tr / _)" "$python" -B "$here/static-profile.py" \
        link-if-same "$target" "$link"
}
component_make()
{
    log_name=$1; part=$2; shift 2
    if [ "${profile:-}" = network ]; then
        set -- "$@" "EMBER_MDNS_SOURCE=$mdns_source" "EMBER_DHCPCD_SOURCE=$dhcpcd_source" \
            "EMBER_LIBPCAP_SOURCE=$libpcap_source" "EMBER_WPA_SOURCE=$wpa_source" \
            $network_auth_flags
        case "$part" in
            external/bsd/wpa/bin/wpa_supplicant|external/bsd/wpa/bin/wpa_cli)
                set -- "$@" EMBER_WPA_PROFILE=personal
                if [ -n "$wpa_backend" ]; then
                    set -- "$@" "EMBER_WPA_CRYPTO_BACKEND=$wpa_backend"
                fi
                if [ -n "$wolfssl_prefix" ]; then
                    set -- "$@" "EMBER_WOLFSSL_PREFIX=$wolfssl_prefix"
                fi ;;
        esac
    fi
    run "$log_name" "$wrapper" -C "$src/$part" "$@"
}
component()
{
    part=$1; shift
    label=$(printf '%s' "$part" | tr / _)
    if [ "${profile:-}" = network ]; then
        link_gc="LDFLAGS.${part##*/}=-Wl,--gc-sections"
        component_make "$label-obj" "$part" "$link_gc" "$@" obj
        component_make "$label-build" "$part" -j"$jobs" "$link_gc" "$@" dependall
        component_make "$label-install" "$part" "$link_gc" "$@" install
    else
        component_make "$label-obj" "$part" "$@" obj
        component_make "$label-build" "$part" -j"$jobs" "$@" dependall
        component_make "$label-install" "$part" "$@" install
    fi
}
# The same component runner also builds a target-independent console subset.
if [ "${1:-}" = --console-selected ]; then
    shift
    src=$1 out=$2 tools=$3 compiler=$4 sdk=$5 host_prefix=$6 host_family=$7
    shift 7
    machine=$1 arch=$2 libedit=$3 llvm_source=$4 host_bfd=$5 byacc=$6
    shift 6
    runtime_prefix=$1 resource=$2 unwinder=$3 compiler_resource=$4 jobs=$5 timestamp=$6 revision=$7
    shift 7
    headers=$1 libraries=$2 programs=$3 python=$4 profile=${5:-console} overlay=${6:-} extra=${7:-} selected_awk=${8:-$host_prefix/bin/nawk}
    wolfssl_prefix=${9:-} network_interface=${10:-} mdns_source=${11:-} dhcpcd_source=${12:-} install_map=${13:-} wpa_backend=${14:-} crunch_programs=${15:-} device_nodes=${16:-} device_links=${17:-} libpcap_source=${18:-} device_make=${19:-} wpa_source=${20:-} device_block_nodes=${21:-} network_root_device=${22:-} network_root_partition=${23:-a} network_root_stage=${24:-} data_components=${25:-}
    if [ "$profile" = network ]; then
        COPTS="${COPTS:+$COPTS }-Oz -ffunction-sections -fdata-sections"
        network_auth_flags='MKPAM=no USE_PAM=no MKKERBEROS=no USE_KERBEROS=no MKLDAP=no USE_LDAP=no MKYP=no USE_YP=no MKSKEY=no USE_SKEY=no MKHESIOD=no USE_HESIOD=no'
        export COPTS
    fi
    wrapper=$out/nbmake-$machine
    wrapper_log=$out/logs/wrapper.log
    if [ "$profile" = network ]; then
        wrapper_log=$out/logs/wrapper-network-$$.log
        wrapper_suffix=0
        while [ -e "$wrapper_log" ]; do
            wrapper_suffix=$((wrapper_suffix + 1))
            wrapper_log=$out/logs/wrapper-network-$$-$wrapper_suffix.log
        done
    fi
    CPPFLAGS="-nostdinc -isystem $out/dest/usr/include -isystem $compiler_resource/include"
    export CPPFLAGS
    set -- -V "EMBER_LLVM_SOURCE=$llvm_source" -V "EMBER_LIBUNWIND_ARCHIVE=$unwinder" \
        -V EMBER_LLVM_RUNTIME_PHASE=bootstrap -V "EMBER_EXTERNAL_LLVM_RUNTIME=$runtime_prefix" \
        -V "EMBER_LLVM_RESOURCE_DIR=$resource" -V MKLIBCXX=no
    if [ "$arch" = riscv32 ]; then set -- "$@" -V MKSOFTFLOAT=yes; fi
    if [ "$profile" = network ]; then
        set -- "$@" -V "COPTS=$COPTS"
    fi
    "$python" -B "$sdk/scripts/run-native-os.py" \
        --host-prefix "$host_prefix" --compiler-family "$host_family" --source "$src" \
        --routing "$out/routing" --cwd "$src" --log "$wrapper_log" --build-sh -- \
        -U -u -j"$jobs" -m "$machine" -a "$arch" -O "$out/obj" -D "$out/dest" -T "$tools" -w "$wrapper" \
        -V TOOLCHAIN_MISSING=yes -V "EXTERNAL_TOOLCHAIN=$compiler" -V EMBER_COMPILER_FAMILY=clang \
        -V "EMBER_HOST_BFD_CONFIG=$host_bfd" -V "EMBER_LIBEDIT_SOURCE=$libedit" -V "EMBER_BYACC_SOURCE=$byacc" \
        -V "CPPFLAGS=$CPPFLAGS" "$@" -V MKCXX=no -V MKCOMPAT=no -V MKPROFILE=no \
        -V MKDEBUG=no -V MKDEBUGLIB=no -V MKMAN=no -V MKPIC=no -V MKPICINSTALL=no \
        -V MKPIE=no -V LDSTATIC=-static -V MKREPRO=yes -V "MKREPRO_TIMESTAMP=$timestamp" \
        -V MAKECONF=/dev/null -V "BUILDINFO=EmberBSD:$revision" makewrapper
    set -f
    for part in $headers; do
        label=$(printf '%s' "$part" | tr / _)
        component_make "$label-obj" "$part" obj
        component_make "$label-includes" "$part" includes
    done
    for part in $libraries; do
        label=$(printf '%s' "$part" | tr / _)
        component_make "$label-obj" "$part" obj
        component_make "$label-includes" "$part" includes
        component "$part"
    done
    if [ "$profile" = console-tools ]; then
        mkdir -p "$out/crunch"
        {
            printf 'srcdirs bin sbin\nprogs'
            for part in $programs; do printf ' %s' "${part##*/}"; done
            printf '\nln sh -sh\nln reboot halt\n'
            printf 'libs -ledit -lterminfo -lutil -lkvm -lprop -lm\n'
        } > "$out/crunch/console-tools.conf"
        (
            cd "$out/crunch"
            export MAKE="$HOST_SH $wrapper"
            run crunchgen "$tools/bin/nbcrunchgen" -f -D "$src" \
                -L "$out/dest/usr/lib" -V SMALLPROG=1 -V MKRUMP=no console-tools.conf
            run crunch-build "$wrapper" -j"$jobs" -f console-tools.mk \
                "MAKE=$HOST_SH $wrapper" "NM=$host_prefix/bin/llvm-nm" \
                "OBJCOPY=$host_prefix/bin/llvm-objcopy" "AWK=$selected_awk" \
                console-tools.crunched
        )
    elif [ "$profile" = network ]; then
        mkdir -p "$out/crunch"
        {
            printf 'srcdirs bin sbin usr.bin libexec external/bsd/dhcpcd/sbin external/bsd/wpa/bin external/apache2/mDNSResponder/usr.sbin\nprogs'
            for part in $crunch_programs; do printf ' %s' "${part##*/}"; done
            printf '\nln sh -sh\nln reboot halt\n'
            printf 'libs -ledit -lterminfo -lutil -lkvm -lprop -lcrypt -lpcap -lipsec -lm'
            case "$wpa_backend" in
                wolfssl) printf ' -lwolfssl' ;;
                openssl|'') printf ' -lcrypto' ;;
                *) echo "Unsupported WPA backend for network crunch: $wpa_backend" >&2; exit 2 ;;
            esac
            printf '\n'
        } > "$out/crunch/console-tools.conf"
        (
            cd "$out/crunch"
            export MAKE="$HOST_SH $wrapper $network_auth_flags"
            set -- -f -V EMBER_GREP_UNCOMPRESSED_ONLY=yes \
                -V 'LDFLAGS.console-tools=-Wl,--gc-sections' -D "$src" \
                -V MKPAM=no -V USE_PAM=no -V MKKERBEROS=no -V USE_KERBEROS=no \
                -V MKLDAP=no -V USE_LDAP=no -V MKYP=no -V USE_YP=no \
                -V MKSKEY=no -V USE_SKEY=no -V MKHESIOD=no -V USE_HESIOD=no \
                -V MKRUMP=no
            for assignment in \
                "EMBER_DHCPCD_SOURCE=$dhcpcd_source" \
                "EMBER_MDNS_SOURCE=$mdns_source" \
                "EMBER_WPA_SOURCE=$wpa_source" \
                "EMBER_WPA_PROFILE=personal" \
                "EMBER_LIBPCAP_SOURCE=$libpcap_source"; do
                set -- "$@" -v "$assignment" -V "$assignment"
            done
            if [ -n "$wpa_backend" ]; then
                set -- "$@" -v "EMBER_WPA_CRYPTO_BACKEND=$wpa_backend" \
                    -V "EMBER_WPA_CRYPTO_BACKEND=$wpa_backend"
            fi
            if [ -n "$wolfssl_prefix" ]; then
                set -- "$@" -v "EMBER_WOLFSSL_PREFIX=$wolfssl_prefix" \
                    -V "EMBER_WOLFSSL_PREFIX=$wolfssl_prefix"
            fi
            set -- "$@" -L "$out/dest/usr/lib"
            if [ "$wpa_backend" = wolfssl ]; then
                [ -n "$wolfssl_prefix" ] || { echo 'Missing WolfSSL prefix.' >&2; exit 2; }
                set -- "$@" -L "$wolfssl_prefix/lib"
            fi
            run crunchgen "$tools/bin/nbcrunchgen" "$@" console-tools.conf
            run crunch-build "$wrapper" -j"$jobs" -f console-tools.mk \
                "MAKE=$HOST_SH $wrapper" "NM=$host_prefix/bin/llvm-nm" \
                "OBJCOPY=$host_prefix/bin/llvm-objcopy" "AWK=$selected_awk" \
                console-tools.crunched
        )
        for part in $programs; do component "$part"; done
        mkdir -p "$out/dest/libexec/dhcpcd-hooks"
        for part in $data_components; do component "$part"; done
        for file in libexec/dhcpcd-run-hooks libexec/dhcpcd-hooks/01-test \
                    libexec/dhcpcd-hooks/20-resolv.conf libexec/dhcpcd-hooks/30-hostname \
                    libexec/dhcpcd-hooks/50-ntp.conf; do
            [ -f "$out/dest/$file" ] || {
                echo "Missing native dhcpcd install output: $file" >&2; exit 1
            }
        done
    else
        for part in $programs; do component "$part" SMALLPROG=1; done
    fi
    run etc-obj "$wrapper" -C "$src/etc" obj
    root=$out/root
    root_spec=$out/root.spec
    if [ "$profile" = network ]; then
        case "$network_root_stage" in "$out"/.network-root-stage-*) ;; *)
            echo 'Network root staging path is missing or outside its output.' >&2; exit 2;;
        esac
        [ -d "$network_root_stage" ] && [ ! -L "$network_root_stage" ] || {
            echo 'Network root staging path is not a real directory.' >&2; exit 2
        }
        root=$network_root_stage
        root_spec=$network_root_stage.spec
    fi
    run makedev "$wrapper" -C "$src/etc" MAKEDEV
    mkdir -p "$root/bin" "$root/sbin" "$root/dev" "$root/etc" "$root/tmp"
    if [ "$profile" = console-tools ]; then
        cp "$out/crunch/console-tools" "$root/bin/console-tools"
        for part in $programs; do
            case "$part" in
                bin/*) link_if_same console-tools "$root/$part" ;;
                sbin/*) link_if_same ../bin/console-tools "$root/$part" ;;
                *) echo 'Invalid console program path.' >&2; exit 1 ;;
            esac
        done
        link_if_same ../bin/console-tools "$root/sbin/halt"
    elif [ "$profile" = network ]; then
        cp "$out/crunch/console-tools" "$root/bin/console-tools"
        for part in $crunch_programs; do
            case "$part" in
                bin/*) link_if_same console-tools "$root/$part" ;;
                sbin/*) link_if_same ../bin/console-tools "$root/$part" ;;
                usr.bin/*) mkdir -p "$root/usr/bin"; link_if_same ../../bin/console-tools "$root/usr/bin/${part##*/}" ;;
                libexec/*) mkdir -p "$root/usr/libexec"; link_if_same ../../bin/console-tools "$root/usr/libexec/${part##*/}" ;;
                external/bsd/dhcpcd/sbin/*) mkdir -p "$root/sbin"; link_if_same ../bin/console-tools "$root/sbin/${part##*/}" ;;
                external/bsd/wpa/bin/*|external/apache2/mDNSResponder/usr.sbin/*) mkdir -p "$root/usr/sbin"; link_if_same ../../bin/console-tools "$root/usr/sbin/${part##*/}" ;;
                *) echo 'Invalid network utility path.' >&2; exit 1 ;;
            esac
        done
        link_if_same ../bin/console-tools "$root/sbin/halt"
    else
        cp "$out/dest/bin/sh" "$root/bin/sh"
        cp "$out/dest/sbin/init" "$root/sbin/init"
    fi
    if [ -n "$extra" ]; then
        extra_name=${extra##*/}
        if [ "$profile" = network ]; then
            [ ! -e "$root/bin/$extra_name" ] && [ ! -L "$root/bin/$extra_name" ] || {
                echo 'Console program conflicts with a base path.' >&2; exit 1
            }
            cp "$extra" "$root/bin/$extra_name"
            chmod 0555 "$root/bin/$extra_name"
        else
            [ ! -e "$root/bin/$extra_name" ] && [ ! -L "$root/bin/$extra_name" ] || {
                echo 'Console program conflicts with a base path.' >&2; exit 1
            }
            cp "$extra" "$root/bin/$extra_name"
            chmod 0555 "$root/bin/$extra_name"
        fi
    fi
    if [ -n "$overlay" ]; then
        if [ "$profile" != network ]; then
            run overlay "$python" -B "$here/static-profile.py" stage-overlay "$overlay" "$root"
        fi
    fi
    chmod 1777 "$root/tmp"
    if [ "$profile" = network ]; then
        [ -n "$overlay" ] && [ -n "$network_interface" ] && [ -n "$mdns_source" ] && [ -n "$dhcpcd_source" ] && [ -n "$wpa_source" ] || {
            echo 'Network profile inputs are incomplete.' >&2; exit 1
        }
        for entry in $install_map; do
            install_path=${entry#*=}
            [ -f "$out/dest/$install_path" ] || {
                echo "Missing network program: $install_path" >&2; exit 1
            }
            mkdir -p "$root/${install_path%/*}"
            cp -p "$out/dest/$install_path" "$root/$install_path"
        done
        [ -x "$root/bin/console-tools" ] || { echo 'Missing network console-tools.' >&2; exit 1; }
        run network-config "$python" -B "$here/static-profile.py" stage-network \
            "$root" "$src" "$overlay" "$network_interface" \
            "$tools/bin/nbpwd_mkdb" "$out/dest" "$network_root_device" "$network_root_partition"
        MACHINE="$machine" MACHINE_ARCH="$arch" "$HOST_SH" "$out/obj/etc/MAKEDEV" -s std $device_make > "$out/devices-all.spec"
        devices=$device_nodes
        device_link_nodes=$device_links
        "$tools/bin/nbmtree" -c -p "$root" -k type,mode,link | \
            sed -E 's/(\/set[[:space:]].*)/\1 uid=0 gid=0/' > "$root_spec"
        run network-target-modes "$python" -B "$here/static-profile.py" \
            apply-metalog-modes "$root_spec" "$root" "$out/dest" "$out/dest/METALOG" "$install_map"
        for node in $devices; do
            awk -v n="$node" '$1 == "./" n && $2 == "type=char" {sub(/^\./,"./dev"); print}' \
                "$out/devices-all.spec" >> "$root_spec"
            [ "$(grep -c "^\./dev/$node type=char " "$root_spec")" -eq 1 ] || {
                echo "Missing or duplicate device: $node" >&2; exit 1
            }
        done
        for node in $device_link_nodes; do
            awk -v n="$node" '$1 == "./" n && $2 == "type=link" {sub(/^\./,"./dev"); print}' \
                "$out/devices-all.spec" >> "$root_spec"
            [ "$(grep -c "^\./dev/$node type=link " "$root_spec")" -eq 1 ] || {
                echo "Missing or duplicate device link: $node" >&2; exit 1
            }
        done
        for node in $device_block_nodes; do
            awk -v n="$node" '$1 == "./" n && $2 == "type=block" {sub(/^\./,"./dev"); print}' \
                "$out/devices-all.spec" >> "$root_spec"
            [ "$(grep -c "^\./dev/$node type=block " "$root_spec")" -eq 1 ] || {
                echo "Missing or duplicate block device: $node" >&2; exit 1
            }
        done
        rm -f "$out/devices-all.spec"
        exit 0
    fi
    if [ "$profile" = console-tools ]; then
        MACHINE="$machine" MACHINE_ARCH="$arch" "$HOST_SH" "$out/obj/etc/MAKEDEV" -s std wscons iic0 > "$out/devices-all.spec"
        devices='console null zero tty klog ttyE0 wsmouse0 iic0'
    else
        MACHINE="$machine" MACHINE_ARCH="$arch" "$HOST_SH" "$out/obj/etc/MAKEDEV" -s std > "$out/devices-all.spec"
        devices='console null zero tty'
    fi
    "$tools/bin/nbmtree" -c -p "$out/root" -k type,mode,link | \
        sed -E 's/(\/set[[:space:]].*)/\1 uid=0 gid=0/' > "$out/root.spec"
    for node in $devices; do
        awk -v n="$node" '$1 == "./" n && $2 == "type=char" {sub(/^\./,"./dev"); print}' \
            "$out/devices-all.spec" >> "$out/root.spec"
        [ "$(grep -c "^\./dev/$node type=char " "$out/root.spec")" -eq 1 ] || {
            echo "Missing or duplicate device: $node" >&2; exit 1
        }
    done
    "$tools/bin/nbmakefs" -t ffs -B little -s 4m -o version=2,bsize=16384,fsize=2048 \
        -F "$out/root.spec" -N "$src/etc" "$out/root.fs" "$out/root"
    exit 0
fi
if [ "${1:-}" != --selected ]; then
    [ "$#" -eq 4 ] || { echo "usage: $0 SOURCE NEW_OUTPUT TOOLDIR TARGET_PREFIX" >&2; exit 2; }
    : "${EMBER_HOST_PREFIX:?Set EMBER_HOST_PREFIX to the selected native SDK prefix}"
    exec "$EMBER_HOST_PREFIX/bin/python3.15" -B "$here/cix-p1-native.py" "$@"
fi
shift
src=$1 out=$2 tools=$3 compiler=$4 sdk=$5 host_prefix=$6 host_family=$7 family=$8
shift 8
libedit=$1 llvm_source=$2 host_bfd=$3 jobs=$4 git=$5 python=$6 shasum=$7
shift 7
libarchive=$1 libevent=$2 byacc=$3 iscsi=$4 libpcap=$5 tcpwrappers=$6
shift 6
libcbor=$1 libfido2=$2 libfetch=$3 mdns=$4 dhcpcd=$5
shift 5
runtime_prefix=$1 resource=$2 unwinder=$3 compiler_resource=$4 version=$5 machine=$6 selected_builtins=$7
nativepython=$host_prefix/bin/python3.15
runner=$sdk/scripts/run-native-os.py
for path in "$src" "$out" "$tools" "$compiler" "$sdk" "$host_prefix" "$host_bfd"; do
    case "$path" in /*) ;; *) echo 'Absolute paths required.' >&2; exit 2;; esac
    case "$path" in *[!A-Za-z0-9_./-]*) echo 'Unsafe build path.' >&2; exit 2;; esac
done
[ -d "$src" ] && [ -f "$src/build.sh" ] || exit 2
[ ! -e "$out/BUILD-INPUTS.txt" ] || { echo "Output already exists." >&2; exit 2; }
# Base headers, libedit and sh consume the source prepared from the Ports recipe.
case "$libedit" in /*) ;; *) echo 'EMBER_LIBEDIT_SOURCE must name the prepared absolute source.' >&2; exit 2;; esac
case "$libedit" in *[!A-Za-z0-9_./-]*) echo 'Unsafe build path.' >&2; exit 2;; esac
[ -f "$libedit/libedit-source.json" ] && [ -f "$libedit/ports.distinfo" ] || {
    echo 'Prepared libedit source is incomplete.' >&2; exit 2
}
for tool in nbmkesdb nbmkcsmapper; do [ -x "$tools/bin/$tool" ]; done
if [ "$family" = clang ]; then
    cc=$compiler/bin/clang
    readelf=$compiler/bin/llvm-readelf
    runtime=$selected_builtins
    gcc_include=$compiler_resource/include
else
    cc=$compiler/bin/aarch64--netbsd-gcc
    readelf=$compiler/bin/aarch64--netbsd-readelf
    [ -x "$cc" ] && [ -x "$readelf" ] || exit 2
    version=$("$cc" -dumpfullversion)
    case "$version" in 16.*) ;; *) echo 'GCC16 is required.' >&2; exit 2;; esac
    machine=$("$cc" -dumpmachine)
    case "$machine" in aarch64--netbsd) ;; *) echo 'AArch64 NetBSD ABI is required.' >&2; exit 2;; esac
    runtime=$("$cc" -print-libgcc-file-name)
    unwinder=$("$cc" -print-file-name=libgcc_eh.a)
    [ -f "$unwinder" ] || unwinder=
    gcc_include=$("$cc" -print-file-name=include)
fi
[ -x "$cc" ] && [ -x "$readelf" ] && [ -f "$runtime" ] || exit 2
revision=$("$git" -C "$src" rev-parse HEAD)
"$git" -C "$src" diff --quiet
"$git" -C "$src" diff --cached --quiet
[ -z "$("$git" -C "$src" ls-files --others --exclude-standard)" ] || {
    echo 'Untracked source inputs are not accepted.' >&2; exit 2
}
case "$jobs" in ''|*[!0-9]*) exit 2;; esac
[ "$jobs" -ge 1 ] || exit 2
# Target headers come from this output; compiler headers come from its selected prefix.
CPPFLAGS="-nostdinc -isystem $out/dest/usr/include -isystem $gcc_include"
export CPPFLAGS
mkdir -p "$out/logs" "$out/dest"
printf '%s\n' "source=$revision" "compiler-family=$family" "compiler-version=$version" "target=$machine" \
    "profile=reduced-static-c-development" > "$out/BUILD-INPUTS.pending"
printf 'target-cppflags=%s\n' "$CPPFLAGS" >> "$out/BUILD-INPUTS.pending"
for input in "$src/build.sh" "$0" "$here/cix-p1-native.py" "$here/cix-p1-i18n.py" "$cc" "$runtime" \
    "$tools/bin/nbmkesdb" "$tools/bin/nbmkcsmapper" "$libedit/libedit-source.json" \
    "$llvm_source/llvm-source.json" "$libarchive/emberbsd-prepared-source.json" \
    "$libevent/emberbsd-prepared-source.json" "$byacc/emberbsd-prepared-source.json" \
    "$iscsi/emberbsd-prepared-source.json" "$libpcap/emberbsd-prepared-source.json" \
    "$tcpwrappers/emberbsd-prepared-source.json" "$libcbor/emberbsd-prepared-source.json" \
    "$libfido2/emberbsd-prepared-source.json" "$libfetch/emberbsd-prepared-source.json" \
    "$mdns/emberbsd-prepared-source.json" "$dhcpcd/emberbsd-prepared-source.json" \
    "$dhcpcd/hooks/dhcpcd-run-hooks" "$dhcpcd/hooks/20-resolv.conf"; do
    "$shasum" -a 256 "$input" >> "$out/BUILD-INPUTS.pending"
done
mkdir -p "$out/dhcpcd-hooks"
cp "$dhcpcd/hooks/dhcpcd-run-hooks" "$out/dhcpcd-hooks/dhcpcd-run-hooks"
cp "$dhcpcd/hooks/20-resolv.conf" "$out/dhcpcd-hooks/20-resolv.conf"
(cd "$out" && "$shasum" -a 256 dhcpcd-hooks/dhcpcd-run-hooks \
    dhcpcd-hooks/20-resolv.conf > DHCPCD-HOOKS-SHA256.txt)
"$shasum" -a 256 "$0" "$here/cix-p1-native.py" "$here/cix-p1-i18n.py" > "$out/PROFILE-SHA256.txt"
if [ -n "$unwinder" ]; then
    "$shasum" -a 256 "$unwinder" >> "$out/BUILD-INPUTS.pending"
else
    echo 'compiler-unwinder=not-provided' >> "$out/BUILD-INPUTS.pending"
fi
wrapper=$out/nbmake-evbarm
set -- -V "EMBER_LLVM_SOURCE=$llvm_source"
if [ "$family" = clang ]; then
    set -- "$@" -V "EMBER_LIBUNWIND_ARCHIVE=$unwinder" \
        -V EMBER_LLVM_RUNTIME_PHASE=bootstrap -V "EMBER_EXTERNAL_LLVM_RUNTIME=$runtime_prefix" \
        -V "EMBER_LLVM_RESOURCE_DIR=$resource" -V MKLIBCXX=no
fi
"$nativepython" -B "$runner" --host-prefix "$host_prefix" --compiler-family "$host_family" \
    --source "$src" --routing "$out/routing" --cwd "$src" --log "$out/logs/wrapper.log" \
    --build-sh -- -U -u -j"$jobs" -m evbarm -a aarch64 \
    -O "$out/obj" -D "$out/dest" -T "$tools" -w "$wrapper" \
    -V TOOLCHAIN_MISSING=yes -V "EXTERNAL_TOOLCHAIN=$compiler" \
    -V "EMBER_COMPILER_FAMILY=$family" -V "EMBER_HOST_BFD_CONFIG=$host_bfd" \
    -V "CPPFLAGS=$CPPFLAGS" -V "EMBER_LIBARCHIVE_SOURCE=$libarchive" \
    -V "EMBER_LIBEVENT_SOURCE=$libevent" -V "EMBER_BYACC_SOURCE=$byacc" \
    -V "EMBER_ISCSI_SOURCE=$iscsi" -V "EMBER_LIBPCAP_SOURCE=$libpcap" \
    -V "EMBER_TCP_WRAPPERS_SOURCE=$tcpwrappers" -V "EMBER_LIBCBOR_SOURCE=$libcbor" \
    -V "EMBER_LIBFIDO2_SOURCE=$libfido2" -V "EMBER_LIBFETCH_SOURCE=$libfetch" \
    -V "EMBER_MDNS_SOURCE=$mdns" -V "EMBER_DHCPCD_SOURCE=$dhcpcd" "$@" \
    -V MKDEBUG=no -V MKDEBUGLIB=no -V MKX11=no -V MKCXX=no -V MKCOMPAT=no \
    -V MKKMOD=no -V MKGCCCMDS=no -V MKGDB=no -V MKBINUTILS=no \
    -V MKATF=no -V MKKYUA=no -V MKMAN=no -V MKDOC=no -V MKINFO=no \
    -V MKHTML=no -V MKLINT=no -V MKPROFILE=no -V MKCVS=no \
    -V MKGROFF=no -V MKRUMP=no -V MKPIC=no -V MKPICINSTALL=no \
    -V MKPIE=no -V LDSTATIC=-static -V MKZFS=no -V MKDTRACE=no \
    -V MKPAM=no -V USE_PAM=no -V MKKERBEROS=no -V USE_KERBEROS=no \
    -V MKLDAP=no -V USE_LDAP=no -V MKYP=no -V USE_YP=no \
    -V MKSKEY=no -V USE_SKEY=no -V MKHESIOD=no -V USE_HESIOD=no \
    -V "EMBER_LIBEDIT_SOURCE=$libedit" -V "BUILDINFO=EmberBSD:$revision" makewrapper
run obj "$wrapper" -C "$src" obj
run directories "$wrapper" -C "$src" do-distrib-dirs
run includes "$wrapper" -C "$src" -j"$jobs" includes
"$shasum" -a 256 "$wrapper" >> "$out/BUILD-INPUTS.pending"
# Rebuild the selected libraries. Old archives used another SSP/header profile.
for part in lib/csu lib/libc lib/libterminfo lib/libedit lib/libutil \
    lib/libcrypt lib/libprop lib/libkvm lib/libm lib/libz lib/libbz2 lib/libipsec \
    external/bsd/libpcap/lib crypto/external/apache2/openssl/lib/libcommon \
    crypto/external/apache2/openssl/lib/libcrypto \
    crypto/external/apache2/openssl/lib/libdes \
    crypto/external/apache2/openssl/lib/libssl external/bsd/blocklist/lib lib/libwrap \
    crypto/external/bsd/netpgp/libmj; do
    component "$part"
done
# libssh is a private object-tree dependency. It has no installed link library.
run openssh-lib-obj "$wrapper" -C "$src/crypto/external/bsd/openssh/lib" obj
run openssh-lib-build "$wrapper" -C "$src/crypto/external/bsd/openssh/lib" -j"$jobs" dependall
for part in bin/sh bin/cat bin/ls bin/cp bin/mv bin/rm bin/mkdir bin/chmod \
    bin/df bin/dd bin/echo bin/sleep bin/sync bin/hostname bin/pwd bin/kill \
    bin/date bin/ps bin/stty sbin/init sbin/mknod sbin/ifconfig sbin/route sbin/dmesg sbin/sysctl \
    sbin/mount sbin/mount_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/mount_ptyfs sbin/umount sbin/reboot \
    sbin/shutdown sbin/rndctl sbin/ping sbin/fsck sbin/fsck_ffs sbin/gpt sbin/dkctl \
    usr.bin/stat usr.bin/wall usr.bin/cksum usr.bin/uname usr.bin/env \
    usr.bin/printf usr.bin/wc usr.bin/nc usr.bin/sed usr.bin/grep \
    usr.bin/head usr.bin/tail external/historical/nawk/bin usr.bin/sort \
    usr.bin/tr usr.bin/mktemp usr.bin/netstat libexec/getty usr.sbin/powerd usr.sbin/usbdevs \
    usr.sbin/envstat sbin/nvmectl sbin/wdogctl usr.sbin/acpitools/acpidump \
    external/bsd/wpa/bin/wpa_passphrase external/bsd/wpa/bin/wpa_supplicant \
    external/bsd/wpa/bin/wpa_cli crypto/external/bsd/openssh/bin/sshd \
    crypto/external/bsd/openssh/libexec/sshd-session crypto/external/bsd/openssh/libexec/sshd-auth \
    crypto/external/bsd/openssh/bin/ssh-keygen usr.bin/passwd usr.sbin/pwd_mkdb; do
    component "$part"
done
# Keep the reduced non-privsep profile, with both IPv4 and IPv6 clients.
component external/bsd/dhcpcd/sbin/dhcpcd SMALLPROG=1 SMALLPROG_INET6=1
component share/i18n/iconv
component share/i18n/esdb 'SUBDIR=ISO646 UTF' ISO646_PART=US UTF_PART=8
component share/i18n/csmapper SUBDIR=ISO646 PART_ISO646=US
printf '%s\n' 'i18n-recipes=iconv;esdb:ISO646-US,UTF-8;csmapper:ISO646-US' >> "$out/BUILD-INPUTS.pending"
"$python" -B "$here/cix-p1-i18n.py" record "$out" "$revision"
printf 'i18n-inventory-sha256=%s\n' "$("$shasum" -a 256 "$out/I18N-INVENTORY.json" | awk '{print $1}')" \
    >> "$out/BUILD-INPUTS.pending"
run makedev "$wrapper" -C "$src/etc" MAKEDEV
for file in bin/sh bin/stty usr/sbin/usbdevs sbin/init sbin/mknod sbin/ifconfig sbin/dhcpcd sbin/fsck \
    sbin/fsck_ffs sbin/mount_msdos sbin/mount_tmpfs sbin/gpt sbin/dkctl sbin/shutdown sbin/rndctl \
    sbin/mount_ptyfs usr/sbin/sshd usr/libexec/sshd-session usr/libexec/sshd-auth \
    usr/bin/ssh-keygen usr/bin/passwd usr/sbin/pwd_mkdb \
    usr/bin/stat usr/bin/wall usr/sbin/wpa_supplicant usr/sbin/wpa_cli usr/sbin/powerd usr/libexec/getty \
    usr/sbin/envstat sbin/nvmectl sbin/wdogctl usr/sbin/acpidump; do
    [ -f "$out/dest/$file" ] || {
        echo "Missing required program: $file" >&2; exit 1
    }
done
# Inspect every installed ELF candidate. This does not execute target binaries.
find "$out/dest/bin" "$out/dest/sbin" "$out/dest/usr/bin" \
    "$out/dest/usr/sbin" "$out/dest/usr/libexec" -type f \
    > "$out/programs.list"
: > "$out/BINARIES-SHA256.txt"
while IFS= read -r file; do
    "$readelf" -h "$file" > "$out/elf-header.tmp"
    grep -q 'Class:.*ELF64' "$out/elf-header.tmp"
    grep -q 'Data:.*little endian' "$out/elf-header.tmp"
    grep -q 'Machine:.*AArch64' "$out/elf-header.tmp"
    grep -q 'Type:.*EXEC' "$out/elf-header.tmp"
    "$readelf" -l "$file" > "$out/elf-segments.tmp"
    if grep -E 'INTERP|DYNAMIC' "$out/elf-segments.tmp"; then
        echo "Nonstatic executable: $file" >&2; exit 1
    fi
    "$shasum" -a 256 "$file" >> "$out/BINARIES-SHA256.txt"
done < "$out/programs.list"
[ -s "$out/BINARIES-SHA256.txt" ]
"$shasum" -a 256 -c "$out/PROFILE-SHA256.txt" >/dev/null
"$python" -B "$here/cix-p1-i18n.py" verify "$out" "$revision" "$out/BUILD-INPUTS.pending" >/dev/null
"$git" -C "$src" diff --quiet
"$git" -C "$src" diff --cached --quiet
[ -z "$("$git" -C "$src" ls-files --others --exclude-standard)" ] || exit 1
[ "$("$git" -C "$src" rev-parse HEAD)" = "$revision" ]
mv "$out/BUILD-INPUTS.pending" "$out/BUILD-INPUTS.txt"
echo "Reduced static C cohort built from $revision. No image or media was written."
