#!/bin/zsh
set -eu
cd -- "$(dirname -- "$0")"
exec python3 neo98.py run
