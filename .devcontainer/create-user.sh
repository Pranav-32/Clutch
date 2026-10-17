#!/usr/bin/env bash
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
# Create the image's nonroot account and its source-mount destination.
set -euo pipefail

readonly username="${1:?Development username is required}"
readonly user_uid="${2:?Development UID is required}"
readonly user_home="/home/${username}"
[[ "$username" =~ ^[a-z_][a-z0-9_-]*$ && "$user_uid" =~ ^[1-9][0-9]*$ ]]

# Ubuntu base images may already own this UID. Reuse the account and its home
# instead of deleting its files or creating conflicting filesystem ownership.
if account="$(getent passwd "$user_uid")"; then
    original_user="${account%%:*}"
    if [[ "$original_user" != "$username" ]]; then
        usermod --login "$username" --home "$user_home" --move-home "$original_user"
    fi
else
    useradd --uid "$user_uid" --create-home --shell /bin/bash "$username"
fi

usermod --shell /bin/bash "$username"
printf '%s ALL=(ALL) NOPASSWD:ALL\n' "$username" > "/etc/sudoers.d/${username}"
chmod 0440 "/etc/sudoers.d/${username}"
install -d -o "$username" "$user_home/workspace"
