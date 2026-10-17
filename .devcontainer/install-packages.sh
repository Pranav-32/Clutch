#!/usr/bin/env bash
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
# Install distribution tools and a verified, versioned LLVM tool suite.
set -euo pipefail

readonly llvm_version="${1:?LLVM major version is required}"
readonly expected_fingerprint="${2:?LLVM key fingerprint is required}"
[[ "$llvm_version" =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid LLVM major version' >&2; exit 1; }

# Read package names as data rather than evaluating a shell command from a file.
mapfile -t packages < <(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' /tmp/packages.txt)
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends "${packages[@]}"

# Bind the repository to its checked key, never to the global APT trust store.
. /etc/os-release
[[ "$ID" == ubuntu && -n "$VERSION_CODENAME" ]]
readonly key_file=/tmp/llvm-snapshot.gpg.key
readonly keyring=/usr/share/keyrings/llvm-archive-keyring.gpg
trap 'rm -f "$key_file"' EXIT
curl --fail --silent --show-error --location \
    https://apt.llvm.org/llvm-snapshot.gpg.key --output "$key_file"
fingerprint="$(gpg --batch --show-keys --with-colons "$key_file" \
    | awk -F: '$1 == "fpr" { print $10; exit }')"
[[ "$fingerprint" == "$expected_fingerprint" ]]
gpg --batch --dearmor --output "$keyring" "$key_file"
printf '%s\n' \
    "deb [signed-by=${keyring}] https://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-${llvm_version} main" \
    > /etc/apt/sources.list.d/llvm.list
apt-get update
apt-get install -y --no-install-recommends \
    "clang-${llvm_version}" "clang-format-${llvm_version}" \
    "clang-tidy-${llvm_version}" "clangd-${llvm_version}" "lld-${llvm_version}"

# Unversioned names let editor settings survive future LLVM upgrades.
for tool in clang clang++ clang-format clang-tidy clangd; do
    ln -s "/usr/bin/${tool}-${llvm_version}" "/usr/local/bin/${tool}"
done

# Package indices are rebuildable metadata, not part of the development image.
rm -rf /var/lib/apt/lists/*
