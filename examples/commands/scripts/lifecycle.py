#!/usr/bin/env python3
# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
"""Demonstrate an optional user script invoked by Clutch's native runner."""

from pathlib import Path
import sys


def main():
    """Inspect configure output, prepare a resource and print forwarded values."""
    action, directory, profile, *arguments = sys.argv[1:]
    build = Path(directory)
    if action == 'configured':
        assert (build / 'compile_commands.json').is_file()
    if action == 'resources':
        build.mkdir(parents=True, exist_ok=True)
        (build / 'resource.txt').write_text('Example resource for ' + profile + '\n',
                                           encoding='utf-8')
    print(f'{action} [{profile}] {build}', *arguments)


if __name__ == '__main__':
    main()
