#!/bin/bash

set -e

REPO="cs01/llvm-project"
DEFAULT_INSTALL_DIR="$HOME/.local/null-safe-clang"

GREEN='\033[0;32m'
NC='\033[0m'

prompt_yn() {
    local prompt="$1"
    local default="$2"
    local response

    if ! [ -t 0 ] && ! [ -e /dev/tty ]; then
        echo "No TTY available, using default: $default"
        [ "$default" = "y" ] && return 0 || return 1
    fi

    if [ "$default" = "y" ]; then
        prompt="$prompt [Y/n]: "
    else
        prompt="$prompt [y/N]: "
    fi

    while true; do
        if [ -t 0 ]; then
            read -p "$prompt" response
        else
            printf "%s" "$prompt" > /dev/tty
            read response < /dev/tty
        fi
        response=${response:-$default}
        case "$response" in
            [Yy]|[Yy][Ee][Ss]) return 0 ;;
            [Nn]|[Nn][Oo]) return 1 ;;
            *) echo "Please answer yes or no." ;;
        esac
    done
}

detect_shell_profile() {
    case "$SHELL" in
        */zsh)
            echo "$HOME/.zshrc"
            ;;
        */bash)
            if [ -f "$HOME/.bashrc" ]; then
                echo "$HOME/.bashrc"
            elif [ -f "$HOME/.bash_profile" ]; then
                echo "$HOME/.bash_profile"
            else
                echo "$HOME/.profile"
            fi
            ;;
        *)
            if [ -f "$HOME/.zshrc" ]; then
                echo "$HOME/.zshrc"
            elif [ -f "$HOME/.bashrc" ]; then
                echo "$HOME/.bashrc"
            elif [ -f "$HOME/.bash_profile" ]; then
                echo "$HOME/.bash_profile"
            else
                echo "$HOME/.profile"
            fi
            ;;
    esac
}

INSTALL_DIR="${INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"

echo ""
printf "${GREEN}Welcome to Nullsafe Clang installer${NC}\n"
echo ""

if [ -z "$SKIP_PROMPTS" ]; then
    echo "This will download and install Nullsafe Clang to:"
    echo "  $INSTALL_DIR"
    echo ""

    if ! prompt_yn "Proceed with installation?" "y"; then
        echo "Installation cancelled."
        exit 0
    fi
    echo ""
fi

if [ -z "$VERSION" ]; then
    echo "Fetching latest release version..."
    VERSION=$(curl -s https://api.github.com/repos/$REPO/releases | \
        jq -r '[.[] | select(.tag_name | startswith("v")) | select(.prerelease == false)] | first | .tag_name')

    if [ -z "$VERSION" ]; then
        echo "Failed to fetch latest version. Set VERSION manually:"
        echo "  VERSION=v0.1.6 $0"
        exit 1
    fi
    echo "Latest version: $VERSION"
fi

echo "Detecting platform..."
OS=$(uname -s)
ARCH=$(uname -m)

if [ "$OS" = "Linux" ] && [ "$ARCH" = "x86_64" ]; then
    RELEASE_FILE="clang-nullsafe-linux-x86_64.tar.gz"
    echo "Detected: Linux x86_64"
elif [ "$OS" = "Darwin" ]; then
    RELEASE_FILE="clang-nullsafe-macos-universal.tar.gz"
    echo "Detected: macOS (Universal)"

    if [ ! -f "/opt/homebrew/opt/zstd/lib/libzstd.1.dylib" ] && [ ! -f "/usr/local/opt/zstd/lib/libzstd.1.dylib" ]; then
        echo ""
        echo "Missing required dependency: zstd"
        echo ""

        if command -v brew >/dev/null 2>&1; then
            if [ -z "$SKIP_PROMPTS" ] && prompt_yn "Install zstd via Homebrew now?" "y"; then
                echo "Installing zstd..."
                brew install zstd
                printf "${GREEN}zstd installed successfully${NC}\n"
            else
                echo "Please install zstd manually:"
                echo "    brew install zstd"
                echo ""
                echo "Then run this installer again."
                exit 1
            fi
        else
            echo "Homebrew not found. Please install Homebrew first:"
            echo "    https://brew.sh"
            echo ""
            echo "Then run this installer again."
            exit 1
        fi
    fi
else
    echo "Unsupported platform: $OS $ARCH"
    echo ""
    echo "For WebAssembly/Playground, visit:"
    echo "    https://cs01.github.io/llvm-project/"
    echo ""
    echo "Or download manually from:"
    echo "    https://github.com/$REPO/releases/tag/$VERSION"
    exit 1
fi

URL="https://github.com/$REPO/releases/download/$VERSION/$RELEASE_FILE"

echo "Downloading Nullsafe Clang $VERSION..."
echo "   $URL"

mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

if command -v curl >/dev/null 2>&1; then
    curl -L "$URL" | tar xz --strip-components=0
elif command -v wget >/dev/null 2>&1; then
    wget -O - "$URL" | tar xz --strip-components=0
else
    echo "Neither curl nor wget found. Please install one of them."
    exit 1
fi

if [ "$OS" = "Darwin" ]; then
    echo "Removing macOS quarantine attribute..."
    xattr -dr com.apple.quarantine "$INSTALL_DIR/bin" 2>/dev/null || true

    if ! command -v xcrun >/dev/null 2>&1; then
        echo ""
        echo "Xcode Command Line Tools not found"
        echo ""

        if [ -z "$SKIP_PROMPTS" ] && prompt_yn "Install Xcode Command Line Tools now?" "y"; then
            echo "Opening Xcode Command Line Tools installer..."
            echo "Please complete the installation dialog, then press Enter to continue."
            xcode-select --install 2>/dev/null || true
            read -p "Press Enter after installation completes..."

            if ! command -v xcrun >/dev/null 2>&1; then
                echo "Xcode Command Line Tools still not found."
                echo "Please ensure installation completed successfully."
                exit 1
            fi
        else
            echo "Please install Xcode Command Line Tools:"
            echo "    xcode-select --install"
            echo ""
            echo "Then run this installer again."
            exit 1
        fi
    fi

    SDK_PATH=$(xcrun --show-sdk-path 2>/dev/null || echo "")
    if [ -n "$SDK_PATH" ]; then
        echo "Configuring macOS SDK integration..."

        mv "$INSTALL_DIR/bin/clang" "$INSTALL_DIR/bin/clang.real"
        mv "$INSTALL_DIR/bin/clang++" "$INSTALL_DIR/bin/clang++.real" 2>/dev/null || true

        cat > "$INSTALL_DIR/bin/clang" << 'WRAPPER_EOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null || echo "")"
if [ -n "$SDK_PATH" ]; then
    exec "$SCRIPT_DIR/clang.real" -isysroot "$SDK_PATH" "$@"
else
    exec "$SCRIPT_DIR/clang.real" "$@"
fi
WRAPPER_EOF

        cat > "$INSTALL_DIR/bin/clang++" << 'WRAPPER_EOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null || echo "")"
if [ -n "$SDK_PATH" ]; then
    exec "$SCRIPT_DIR/clang++.real" -isysroot "$SDK_PATH" "$@"
else
    exec "$SCRIPT_DIR/clang++.real" "$@"
fi
WRAPPER_EOF

        chmod +x "$INSTALL_DIR/bin/clang" "$INSTALL_DIR/bin/clang++"
    fi
fi

echo ""
printf "${GREEN}Successfully installed to: $INSTALL_DIR${NC}\n"
echo ""

SHELL_PROFILE=$(detect_shell_profile)
PATH_EXPORT="export PATH=\"$INSTALL_DIR/bin:\$PATH\""

if [ -z "$SKIP_PROMPTS" ]; then
    echo "To use Nullsafe Clang, we need to add it to your PATH."
    echo "Detected shell profile: $SHELL_PROFILE"
    echo ""

    if grep -q "$INSTALL_DIR/bin" "$SHELL_PROFILE" 2>/dev/null; then
        printf "${GREEN}PATH already configured in $SHELL_PROFILE${NC}\n"
        echo ""
        echo "If you just configured it, reload your shell:"
        echo "    source $SHELL_PROFILE"
    else
        if prompt_yn "Add Nullsafe Clang to PATH in $SHELL_PROFILE?" "y"; then
            echo "" >> "$SHELL_PROFILE"
            echo "# Nullsafe Clang" >> "$SHELL_PROFILE"
            echo "$PATH_EXPORT" >> "$SHELL_PROFILE"
            printf "${GREEN}Added to $SHELL_PROFILE${NC}\n"
            echo ""

            if prompt_yn "Activate it now in this shell?" "y"; then
                export PATH="$INSTALL_DIR/bin:$PATH"
                printf "${GREEN}Activated! You can now use 'clang' in this shell.${NC}\n"
            else
                echo "Reload your shell later to activate:"
                echo "    source $SHELL_PROFILE"
                echo ""
                echo "Or open a new terminal window."
            fi
        else
            echo ""
            echo "Skipped PATH configuration. To add it manually, add this to $SHELL_PROFILE:"
            echo "    $PATH_EXPORT"
        fi
    fi
    echo ""

    if prompt_yn "Verify installation now?" "y"; then
        echo ""
        echo "Testing Nullsafe Clang..."

        if command -v clang >/dev/null 2>&1 && clang --version 2>&1 | grep -q "cs01/llvm-project"; then
            PATH_ACTIVE=true
        else
            export PATH="$INSTALL_DIR/bin:$PATH"
            PATH_ACTIVE=false
        fi

        if "$INSTALL_DIR/bin/clang" --version >/dev/null 2>&1; then
            echo ""
            "$INSTALL_DIR/bin/clang" --version | head -n 1
            echo ""
            printf "${GREEN}Installation verified successfully!${NC}\n"
            echo ""

            if [ "$PATH_ACTIVE" = true ]; then
                echo "Try it now:"
                echo "    echo 'void f(int* _Nullable p) { *p = 42; }' | clang -x c - -fsyntax-only"
            else
                echo "Try it now (using full path):"
                echo "    echo 'void f(int* _Nullable p) { *p = 42; }' | $INSTALL_DIR/bin/clang -x c - -fsyntax-only"
                echo ""
                echo "Or reload your shell and use just 'clang'."
            fi
        else
            echo "Warning: clang --version failed"
            echo "There may be an issue with the installation."
        fi
    fi
else
    echo "To use Nullsafe Clang, add this to $SHELL_PROFILE:"
    echo "    $PATH_EXPORT"
fi

echo ""
echo "Try the interactive WebAssembly playground:"
echo "    https://cs01.github.io/llvm-project/"
echo ""
