#!/bin/bash
# Shared guardrails for Amiga FFS HDF packaging.

hdf_assert_not_in_use() {
    local hdf="$1"
    if command -v fuser >/dev/null 2>&1 && [ -f "$hdf" ] && fuser "$hdf" >/dev/null 2>&1; then
        echo "HDF is in use: $hdf" >&2
        echo "Close Amiberry before replacing it." >&2
        return 1
    fi
}

hdf_validate_ffs() {
    local hdf="$1"
    local root

    if [ ! -f "$hdf" ]; then
        echo "missing HDF: $hdf" >&2
        return 1
    fi

    if ! xdftool "$hdf" bitmap info >/dev/null; then
        echo "invalid HDF bitmap: $hdf" >&2
        return 1
    fi

    if ! root="$(xdftool "$hdf" root show)"; then
        echo "invalid HDF root block: $hdf" >&2
        return 1
    fi

    if ! printf '%s\n' "$root" | grep -q 'valid:     True'; then
        echo "invalid HDF root checksum: $hdf" >&2
        return 1
    fi

    if ! printf '%s\n' "$root" | grep -q 'bmp flag:  0xffffffff'; then
        echo "dirty HDF bitmap flag: $hdf" >&2
        return 1
    fi
}

hdf_install_verified() {
    local src="$1"
    shift
    local dst

    hdf_validate_ffs "$src" || return 1
    for dst in "$@"; do
        hdf_assert_not_in_use "$dst" || return 1
    done
    for dst in "$@"; do
        mkdir -p "$(dirname "$dst")"
        cp -f "$src" "$dst"
        chmod 0644 "$dst"
    done
}
