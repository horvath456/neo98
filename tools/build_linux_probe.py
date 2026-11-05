#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the headless Linux runner for guest tests and benchmarks."""
import argparse
from native_app import build_probe, native_x64


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vulkan', action='store_true')
    args = parser.parse_args()
    if not native_x64():
        parser.error('This runner requires Linux x86_64')
    print(build_probe(vulkan=args.vulkan))


if __name__ == '__main__':
    main()
