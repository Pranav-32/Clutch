#!/usr/bin/env bash
#
# Copyright (c) 2026, Pranav Sanjeevi Kumari
# SPDX-License-Identifier: BSD-3-Clause
#
# See LICENSE in the project root for the full license text.
#

# Remove generated folders inside tests/work on Linux, macOS or in a container.
# Usage: bash tests/clean-work.sh [--dry-run|-n]
# Resolves work relative to this script, preserves its root and loose files,
# and preflights every target before deletion. Symbolic links and redirected
# directories inside target trees are rejected. Uses Bash 3.2-compatible syntax.
set -euo pipefail

# Print the accepted options without changing or enumerating any workspaces.
usage() {
    printf 'Usage: bash tests/clean-work.sh [--dry-run|-n]\n'
}

if [[ $# -gt 1 ]]; then
    usage >&2
    exit 2
fi

dry_run=false
case "${1-}" in
    '') ;;
    --dry-run|-n) dry_run=true ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

# Abort during preflight so no validated earlier folder is deleted on failure.
fail() {
    printf 'Cleanup refused: %s\n' "$1" >&2
    exit 1
}

[[ ! -L "${BASH_SOURCE[0]}" ]] || fail 'the cleanup script is a symbolic link'
script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
work_dir="$script_dir/work"

[[ ! -L "$work_dir" ]] || fail 'tests/work is a symbolic link'

if [[ ! -e "$work_dir" ]]; then
    printf 'No tests/work directory to clean.\n'
    exit 0
fi

[[ -d "$work_dir" ]] || fail 'tests/work must be a regular directory'
resolved_work="$(CDPATH= cd -- "$work_dir" && pwd -P)"
[[ "$resolved_work" == "$work_dir" ]] || fail 'tests/work is a redirected directory'

# Match hidden folders and make empty directories produce no wildcard entries.
shopt -s dotglob nullglob

# Require one literal entry to remain under work and reject symbolic links.
validate_entry() {
    case "$1" in
        "$work_dir"/*) ;;
        *) fail "path outside tests/work: $1" ;;
    esac

    [[ ! -L "$1" ]] || fail "symbolic link inside a target tree: $1"
}

# Inspect every descendant explicitly before the deletion phase starts.
# Physical-directory checks supplement lexical checks against redirected paths.
validate_tree() {
    local entry resolved
    validate_entry "$1"
    resolved="$(CDPATH= cd -- "$1" && pwd -P)"
    case "$resolved" in
        "$work_dir"/*) ;;
        *) fail "redirected directory outside tests/work: $1" ;;
    esac

    for entry in "$1"/*; do
        validate_entry "$entry"
        if [[ -d "$entry" ]]; then
            validate_tree "$entry"
        fi
    done
}

# Validate all folders before removing any, including nested links. Loose files
# at the work root are excluded from cleanup. No external deletion root is accepted.
folders=()
folder_count=0
for folder in "$work_dir"/*; do
    [[ -d "$folder" ]] || continue
    validate_tree "$folder"
    folders+=("$folder")
    folder_count=$((folder_count + 1))
done

if [[ "$folder_count" -eq 0 ]]; then
    printf 'No test work folders to clean.\n'
    exit 0
fi

# All targets passed preflight. Delete only those folders, or print a preview.
for folder in "${folders[@]}"; do
    if [[ "$dry_run" == true ]]; then
        printf 'Would remove: %q\n' "$folder"
    else
        rm -rf -- "$folder"
    fi
done

if [[ "$dry_run" == true ]]; then
    printf 'Preview: %s test work folder(s).\n' "$folder_count"
else
    printf 'Removed %s test work folder(s).\n' "$folder_count"
fi
