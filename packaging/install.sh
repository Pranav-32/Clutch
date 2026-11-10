#!/bin/sh
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
#
# Install a Clutch release on Linux or macOS.
#
#   curl -fsSL https://github.com/Pranav-32/Clutch/releases/latest/download/install.sh | sh
#   sh install.sh [--version X.Y.Z] [--prefix DIR] [--target TARGET] [--modify-path]
#   sh install.sh --archive clutch-X.Y.Z-<target>.tar.gz   # offline; needs .sha256 beside it
#
# Installs <prefix>/bin/clutch and its license notices (default prefix:
# ~/.local). Every archive is verified against its published SHA-256 before
# extraction. The shell profile changes only with --modify-path.
set -eu

repository="Pranav-32/Clutch"
prefix="${HOME}/.local"
version=""
archive=""
modify_path=0
target=""

fail() {
  printf 'clutch install: %s\n' "$1" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: install.sh [--version X.Y.Z] [--prefix DIR] [--target TARGET] [--modify-path]
       install.sh --archive clutch-X.Y.Z-<target>.tar.gz [--prefix DIR]

  --version      Release to install (default: latest)
  --target       Release build to install (default: detected for this host):
                 linux-x86_64-gcc, linux-x86_64-clang,
                 macos-arm64-apple-clang or macos-x86_64-apple-clang
  --prefix       Installation prefix (default: ~/.local); installs <prefix>/bin/clutch
  --archive      Install a downloaded archive; its .sha256 file must be beside it
  --modify-path  Append <prefix>/bin to PATH in your shell profile if missing
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) [ "$#" -ge 2 ] || fail "--version needs a value"; version="${2#v}"; shift 2 ;;
    --prefix) [ "$#" -ge 2 ] || fail "--prefix needs a value"; prefix="$2"; shift 2 ;;
    --archive) [ "$#" -ge 2 ] || fail "--archive needs a value"; archive="$2"; shift 2 ;;
    --target) [ "$#" -ge 2 ] || fail "--target needs a value"; target="$2"; shift 2 ;;
    --modify-path) modify_path=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) fail "unknown option: $1 (see --help)" ;;
  esac
done

if [ -n "$target" ]; then
  case "$target" in
    linux-x86_64-gcc|linux-x86_64-clang|macos-arm64-apple-clang|macos-x86_64-apple-clang) ;;
    *) fail "unsupported target: $target (see --help)" ;;
  esac
else
  case "$(uname -s)/$(uname -m)" in
    Linux/x86_64|Linux/amd64) target="linux-x86_64-gcc" ;;
    Darwin/arm64|Darwin/aarch64) target="macos-arm64-apple-clang" ;;
    Darwin/x86_64) target="macos-x86_64-apple-clang" ;;
    *) fail "no prebuilt Clutch for $(uname -s) $(uname -m); build from source instead" ;;
  esac
fi

download() {
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL --proto '=https' --tlsv1.2 -o "$2" "$1"
  elif command -v wget >/dev/null 2>&1; then
    wget -q --https-only -O "$2" "$1"
  else
    fail "curl or wget is required to download releases"
  fi
}

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d ' ' -f 1
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | cut -d ' ' -f 1
  else
    fail "sha256sum or shasum is required to verify the download"
  fi
}

work="$(mktemp -d "${TMPDIR:-/tmp}/clutch-install.XXXXXX")"
trap 'rm -rf "$work"' EXIT INT TERM

if [ -n "$archive" ]; then
  [ -f "$archive" ] || fail "archive not found: $archive"
  [ -f "$archive.sha256" ] || fail "missing checksum file: $archive.sha256"
  cp "$archive" "$work/package.tar.gz"
  cp "$archive.sha256" "$work/package.sha256"
  name="$(basename "$archive")"
else
  if [ -z "$version" ]; then
    download "https://api.github.com/repos/${repository}/releases/latest" "$work/latest.json"
    version="$(sed -n 's/.*"tag_name": *"v\{0,1\}\([^"]*\)".*/\1/p' "$work/latest.json" | head -n 1)"
    [ -n "$version" ] || fail "cannot determine the latest release; pass --version"
  fi
  name="clutch-${version}-${target}.tar.gz"
  base="https://github.com/${repository}/releases/download/v${version}"
  printf 'Downloading %s\n' "$name"
  download "$base/$name" "$work/package.tar.gz"
  download "$base/$name.sha256" "$work/package.sha256"
fi

expected="$(cut -d ' ' -f 1 "$work/package.sha256")"
recorded="$(sed -n 's/^[0-9a-fA-F]\{64\}  *//p' "$work/package.sha256")"
[ "$recorded" = "$name" ] || fail "checksum file names '$recorded', not '$name'"
actual="$(sha256 "$work/package.tar.gz")"
[ "$expected" = "$actual" ] || fail "SHA-256 mismatch for $name"

mkdir "$work/extract"
tar -xzf "$work/package.tar.gz" -C "$work/extract"
root="$(find "$work/extract" -mindepth 1 -maxdepth 1 -type d -name 'clutch-*' | head -n 1)"
[ -n "$root" ] && [ -x "$root/bin/clutch" ] || fail "archive does not contain bin/clutch"

mkdir -p "$prefix/bin" "$prefix/share/licenses/clutch"
# Replace the executable atomically so a running clutch is never truncated.
cp "$root/bin/clutch" "$prefix/bin/.clutch.new"
chmod 755 "$prefix/bin/.clutch.new"
mv -f "$prefix/bin/.clutch.new" "$prefix/bin/clutch"
cp "$root"/share/licenses/clutch/* "$prefix/share/licenses/clutch/"

bin="$(cd "$prefix/bin" && pwd)"
printf 'Installed %s\n' "$("$bin/clutch" --version)"
printf '  %s/clutch\n' "$bin"

case ":${PATH}:" in
  *":${bin}:"*) exit 0 ;;
esac

line="export PATH=\"${bin}:\$PATH\""
if [ "$modify_path" -eq 1 ]; then
  case "${SHELL:-}" in
    */zsh) profile="${ZDOTDIR:-$HOME}/.zshrc" ;;
    */bash) profile="$HOME/.bashrc" ;;
    *) profile="$HOME/.profile" ;;
  esac
  if [ -f "$profile" ] && grep -Fqx "$line" "$profile"; then
    printf '%s already adds %s to PATH\n' "$profile" "$bin"
  else
    printf '\n# Added by the Clutch installer\n%s\n' "$line" >> "$profile"
    printf 'Added %s to PATH in %s; open a new shell to use it.\n' "$bin" "$profile"
  fi
else
  printf '%s is not on PATH. Add this line to your shell profile:\n  %s\n' "$bin" "$line"
  printf 'or rerun with --modify-path.\n'
fi
