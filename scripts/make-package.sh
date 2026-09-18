#!/usr/bin/env bash
# Builds an installable Arch Linux package (.pkg.tar.zst) from the current
# working copy, including uncommitted changes.
#
#   scripts/make-package.sh            builds into dist/
#   scripts/make-package.sh --install  builds and installs with pacman -U
#
# The version is <project version>.r<commit count>.g<short hash>, so a newer
# build always upgrades an older one.
set -euo pipefail

root_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
dist_dir="$root_dir/dist"
work_dir="$root_dir/.build-pkg"
install=0

for arg in "$@"; do
    case "$arg" in
        --install) install=1 ;;
        -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

for tool in makepkg cmake rsync; do
    command -v "$tool" >/dev/null || { echo "$tool is required" >&2; exit 1; }
done

base_version=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$root_dir/CMakeLists.txt" | head -n1)
if git -C "$root_dir" rev-parse --git-dir >/dev/null 2>&1; then
    commits=$(git -C "$root_dir" rev-list --count HEAD)
    hash=$(git -C "$root_dir" rev-parse --short HEAD)
    version="$base_version.r$commits.g$hash"
else
    version="$base_version"
fi

src_name="sequel-ace-$version"
rm -rf "$work_dir"
mkdir -p "$work_dir/$src_name" "$dist_dir"

rsync -a --delete \
    --exclude '/build/' --exclude '/build-*/' --exclude '/dist/' \
    --exclude '/.build-pkg/' --exclude '/.git/' \
    --exclude '/packaging/arch/' \
    "$root_dir/" "$work_dir/$src_name/"
tar -C "$work_dir" -czf "$work_dir/$src_name.tar.gz" "$src_name"
rm -rf "$work_dir/$src_name"

sed "s/^pkgver=.*/pkgver=$version/" "$root_dir/packaging/arch/PKGBUILD" > "$work_dir/PKGBUILD"

export MAKEFLAGS="${MAKEFLAGS:--j$(nproc)}"
export PKGDEST="$dist_dir"
(cd "$work_dir" && makepkg --force --cleanbuild --noconfirm)

pkg_file=$(ls -t "$dist_dir"/sequel-ace-"$version"-*.pkg.tar.* | head -n1)
echo
echo "Package: $pkg_file"
if [ "$install" = 1 ]; then
    sudo pacman -U "$pkg_file"
else
    echo "Install with: sudo pacman -U $pkg_file"
fi
