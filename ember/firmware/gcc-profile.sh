# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted GCC16 firmware profile.
# Source this file from Bash. It does not change the calling environment.

firmware_gcc_query()
(
    unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH \
        LIBRARY_PATH COMPILER_PATH GCC_EXEC_PREFIX LD_RUN_PATH LD_LIBRARY_PATH \
        CFLAGS CPPFLAGS CXXFLAGS ASFLAGS LDFLAGS LDLIBS \
        CC CXX CPP AS LD AR NM OBJCOPY OBJDUMP RANLIB STRIP \
        MAKEFLAGS MFLAGS MAKEOVERRIDES
    "$@"
)

firmware_gcc_profile()
{
    local compiler=$1 machine=$2 version=$3 empty_root=$4 specs=$5
    local resolved=$compiler link count=0 prefix expected

    if [[ ! -d "$empty_root" || -L "$empty_root" || \
        -n "$(ls -A "$empty_root")" ]]; then
        echo 'firmware search root must be an empty directory, not a symlink' >&2
        return 1
    fi
    while [[ -L "$resolved" ]]; do
        count=$((count + 1))
        [[ $count -le 40 ]] || { echo 'target compiler symlink loop' >&2; return 1; }
        link=$(readlink "$resolved") || return 1
        case "$link" in
            /*) resolved=$link ;;
            *) resolved="$(dirname "$resolved")/$link" ;;
        esac
    done
    prefix=$(cd "$(dirname "$resolved")/.." && pwd -P) || return 1
    firmware_gcc_include=$(firmware_gcc_query "$compiler" -print-file-name=include) || return 1
    case "$firmware_gcc_include" in
        /*) ;;
        *) echo 'target compiler include path must be absolute' >&2; return 1 ;;
    esac
    if [[ "$compiler$firmware_gcc_include$specs$empty_root" == *[[:space:]]* ]]; then
        echo 'firmware compiler and profile paths must not contain whitespace' >&2
        return 1
    fi
    firmware_gcc_include=$(cd "$firmware_gcc_include" && pwd -P) || return 1
    expected=$(cd "$prefix/lib/gcc/$machine/$version/include" && pwd -P) || return 1
    if [[ "$firmware_gcc_include" != "$expected" || \
        ! -f "$firmware_gcc_include/stddef.h" || \
        ! -f "$firmware_gcc_include/stdint.h" || \
        ! -f "$firmware_gcc_include/stdarg.h" ]]; then
        echo 'firmware requires builtin headers from the selected compiler' >&2
        return 1
    fi
    [[ -f "$specs" ]] || { echo 'missing firmware GCC specs' >&2; return 1; }
    firmware_gcc_cpp=("--sysroot=$empty_root" -ffreestanding -nostdinc \
        -U__NetBSD__ "-isystem$firmware_gcc_include")
    firmware_gcc_link=("--sysroot=$empty_root" "-specs=$specs" \
        -nostdlib -static -Wl,-nostdlib)
}
