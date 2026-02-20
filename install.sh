#!/bin/bash

set -e

REPO="cs01/llvm-project"
BRANCH="null-safe-c-dev"
INSTALLER_URL="https://raw.githubusercontent.com/$REPO/$BRANCH/install-interactive.sh"

echo "Downloading Nullsafe Clang installer..."
TMPFILE=$(mktemp)
trap 'rm -f "$TMPFILE"' EXIT

if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$INSTALLER_URL" -o "$TMPFILE"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$TMPFILE" "$INSTALLER_URL"
else
    echo "Error: Neither curl nor wget found. Please install one of them."
    exit 1
fi

chmod +x "$TMPFILE"
exec bash "$TMPFILE" "$@" < /dev/tty
