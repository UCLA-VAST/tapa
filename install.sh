#!/bin/sh

# Copyright (c) 2024 RapidStream Design Automation, Inc. and contributors.
# All rights reserved. The contributor(s) of this file has/have agreed to the
# RapidStream Contributor License Agreement.

# This script is used to install TAPA on the target machine.
#
# Usage:
#   ./install.sh [-q]
#   TAPA_LOCAL_PACKAGE=./path/to/tapa.tar.gz ./install.sh
#
# If the user runs this script with the root privilege, it will install the software
# in the /opt/tapa directory. It further creates symbolic links in the
# /usr/local/bin directory to the executables in the /opt/tapa directory to
# make the software available in the system path.
#
# Otherwise, if the user runs this script without the root privilege, it will install
# the software in the $HOME/.tapa directory. It further modifies the user's
# PATH environment variable to include the $HOME/.tapa directory.

# Treat unset variables as an error when substituting. And exit immediately if a
# pipeline returns non-zero status.
set -ue

# Default values for the installation options.
TAPA_LOCAL_PACKAGE="${TAPA_LOCAL_PACKAGE:-}"
TAPA_VERSION="${TAPA_VERSION:-latest}"
TAPA_DOWNLOAD_TMPFILE=""

if [ "$(id -u)" -eq 0 ]; then
  # Default to /opt/tapa if the user has the root privilege.
  TAPA_INSTALL_DIR="${TAPA_INSTALL_DIR:-/opt/tapa}"
  CREATE_SYMLINKS="yes"
  MODIFY_PROFILE_PATH="no"

elif [ "$(id -u)" -ne 0 ]; then
  # Default to the user's home directory if the user does not have the root privilege.
  TAPA_INSTALL_DIR="${TAPA_INSTALL_DIR:-$HOME/.tapa}"
  CREATE_SYMLINKS="no"
  MODIFY_PROFILE_PATH="yes"

fi

VERBOSE="${VERBOSE:-yes}"
QUIET="${QUIET:-no}"

# Display the usage of this script.
usage() {
  cat <<EOF
install.sh - Install TAPA.

Usage: ./install.sh [OPTIONS]
       TAPA_LOCAL_PACKAGE=./tapa.tar.gz ./install.sh [OPTIONS]

Options:
  -t, --target <directory>     Specify the directory to install the software to.
      --no-create-symlinks     Do not create symbolic links in the system path.
      --no-modify-path         Do not modify the PATH environment variable.

  -V, --version <version>      Specify the TAPA version to download (default: latest).
                               Ignored if TAPA_LOCAL_PACKAGE is set.

  -q, --quiet                  Disable verbose output.
  -qq, --quiet-all             Disable most of the output.

  -h, --help                   Display this help message and exit.

Environment variables:
  TAPA_LOCAL_PACKAGE           Path to a local TAPA tarball (skips download).
  TAPA_VERSION                 Version to download (default: latest).
EOF
}

# Main function of this script.
main() {
  # Parse the command-line arguments.
  parse_args "$@"

  if [ -z "$TAPA_LOCAL_PACKAGE" ]; then
    download_tapa_package
  fi

  # Display the installation options if the verbose mode is enabled.
  if [ "$VERBOSE" = "yes" ]; then
    echo "Please verify the specified installation options:"
    echo "  Local package:     $TAPA_LOCAL_PACKAGE"
    echo "  Install target:    $TAPA_INSTALL_DIR"
    echo "  Create symlinks:   $CREATE_SYMLINKS"
    echo "  Modify PATH:       $MODIFY_PROFILE_PATH"
    printf "Press Enter to continue, or Ctrl+C to cancel..."
    read -r _
  fi

  # Check if the installation directory exists.
  check_install_dir

  # Display the installation message.
  if [ "$QUIET" = "no" ]; then
    echo "Installing TAPA to \"$TAPA_INSTALL_DIR\"..."
  fi

  # Extract the TAPA package.
  extract_tapa_package

  # Create symbolic links in the system path.
  create_symlinks

  # Modify the PATH environment variable.
  modify_profile_path
}

# Parse the command-line arguments.
parse_args() {
  while [ $# -gt 0 ]; do
    case "$1" in
    -t | --target)
      TAPA_INSTALL_DIR="$2"
      shift 2
      ;;
    --target=*)
      TAPA_INSTALL_DIR="${1#*=}"
      shift
      ;;
    --no-create-symlinks)
      CREATE_SYMLINKS="no"
      shift
      ;;
    --no-modify-path)
      MODIFY_PROFILE_PATH="no"
      shift
      ;;
    -V | --version)
      TAPA_VERSION="$2"
      shift 2
      ;;
    --version=*)
      TAPA_VERSION="${1#*=}"
      shift
      ;;
    -q | --quiet)
      VERBOSE="no"
      shift
      ;;
    -qq | --quiet-all)
      VERBOSE="no"
      QUIET="yes"
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
    esac
  done
}

# Check if the installation directory exists. If so, prompt the user to confirm.
check_install_dir() {
  # If the installation directory exists
  if [ -d "$TAPA_INSTALL_DIR" ]; then

    # If the user does not enable the auto-confirm option
    if [ "$VERBOSE" = "yes" ]; then
      # Prompt the user to confirm
      printf "The installation directory already exists. Do you want to overwrite it? [y/N]: "
      read -r answer

      # If the user does not confirm
      if [ "$answer" != "y" ] && [ "$answer" != "Y" ]; then
        # Abort the installation
        echo "Aborted. No changes were made."
        exit 1
      fi
    fi

    # If the user enables the auto-confirm option or confirms the prompt,
    # show the message that the installation directory will be overwritten.
    if [ "$QUIET" = "no" ]; then
      echo "Overwriting the installation directory: \"$TAPA_INSTALL_DIR\"..."
    fi

    # Remove the existing installation directory
    rm -rf "$TAPA_INSTALL_DIR"
  fi
}

# Download the TAPA package from GitHub Releases.
download_tapa_package() {
  if [ "$TAPA_VERSION" = "latest" ]; then
    TAPA_URL="https://github.com/tuna/tapa/releases/latest/download/tapa.tar.gz"
  else
    TAPA_URL="https://github.com/tuna/tapa/releases/download/v${TAPA_VERSION}/tapa.tar.gz"
  fi
  if [ "$QUIET" = "no" ]; then
    echo "Downloading TAPA ${TAPA_VERSION} from: ${TAPA_URL}..."
  fi
  TAPA_DOWNLOAD_TMPFILE="$(mktemp /tmp/tapa-XXXXXX.tar.gz)"
  trap 'rm -f "$TAPA_DOWNLOAD_TMPFILE"' EXIT INT TERM
  if command -v curl > /dev/null 2>&1; then
    if [ "$QUIET" = "yes" ]; then
      curl -fsSL -o "$TAPA_DOWNLOAD_TMPFILE" "$TAPA_URL"
    else
      curl -fL --progress-bar -o "$TAPA_DOWNLOAD_TMPFILE" "$TAPA_URL"
    fi
  elif command -v wget > /dev/null 2>&1; then
    wget -q -O "$TAPA_DOWNLOAD_TMPFILE" "$TAPA_URL" 2>&1 || wget -O "$TAPA_DOWNLOAD_TMPFILE" "$TAPA_URL"
  else
    echo "Error: Neither curl nor wget is available. Please install one of them."
    exit 1
  fi
  TAPA_LOCAL_PACKAGE="$TAPA_DOWNLOAD_TMPFILE"
}

# Extract the TAPA package from the local tarball.
extract_tapa_package() {
  if [ ! -f "$TAPA_LOCAL_PACKAGE" ]; then
    echo "Error: Local package not found: \"$TAPA_LOCAL_PACKAGE\""
    exit 1
  fi

  # Extract the TAPA package.
  if [ "$VERBOSE" = "yes" ]; then
    echo "Extracting TAPA from: \"$TAPA_LOCAL_PACKAGE\" to: \"$TAPA_INSTALL_DIR\"..."
  elif [ "$QUIET" = "no" ]; then
    echo "Extracting TAPA..."
  fi
  mkdir -p "$TAPA_INSTALL_DIR"
  tar -xzf "$TAPA_LOCAL_PACKAGE" -C "$TAPA_INSTALL_DIR" --overwrite
}

# Create symbolic links in the system path.
create_symlinks() {
  if [ "$CREATE_SYMLINKS" = "yes" ]; then
    if [ "$QUIET" = "no" ]; then
      echo "Creating symbolic links in the system path \"/usr/local/bin\"..."
    fi

    # Create symbolic links for each executable in the installation directory.
    for bin in "$TAPA_INSTALL_DIR"/usr/bin/*; do
      # Skip the directories.
      if [ ! -f "$bin" ]; then
        continue
      fi

      bin_name="$(basename "$bin")"
      if [ "$VERBOSE" = "yes" ]; then
        echo "Creating symbolic link: \"/usr/local/bin/$bin_name\" -> \"$bin\"..."
      fi
      ln -sf "$bin" "/usr/local/bin/$bin_name"
    done

    # A system-wide install is reached through the symlinks above, so no
    # profile is edited and nothing records where TAPA actually lives. The
    # cosim runtime normally recovers that from the host binary's RUNPATH,
    # but TAPA_HOME is the documented override and the fallback on platforms
    # whose loader cannot report a resolved path.
    if [ -d /etc/profile.d ]; then
      if [ "$VERBOSE" = "yes" ]; then
        echo "Writing \"/etc/profile.d/tapa.sh\"..."
      fi
      echo "export TAPA_HOME=\"$TAPA_INSTALL_DIR\"" >/etc/profile.d/tapa.sh
      chmod 644 /etc/profile.d/tapa.sh
    fi
  fi
}

modify_profile_path_in_file() {
  profile_file="$1"

  # Check if the profile file exists.
  if [ ! -f "$profile_file" ]; then
    if [ "$VERBOSE" = "yes" ]; then
      echo "The profile file \"$profile_file\" does not exist. Skipping..."
    fi
    return
  fi

  # Check if the PATH environment variable is already modified.
  if grep -q "$TAPA_INSTALL_DIR" "$profile_file"; then
    if [ "$VERBOSE" = "yes" ]; then
      echo "The PATH to TAPA is already set in \"$profile_file\". Skipping..."
    fi
    return
  fi

  # Add the PATH environment variable to the profile file. TAPA_HOME goes
  # alongside it: the cosim runtime uses it to locate the DPI libraries when
  # the host binary's RUNPATH cannot be consulted.
  if [ "$QUIET" = "no" ]; then
    echo "Adding PATH to TAPA to \"$profile_file\"..."
  fi
  echo "export PATH=\"\$PATH:$TAPA_INSTALL_DIR/usr/bin\"" >>"$profile_file"
  echo "export TAPA_HOME=\"$TAPA_INSTALL_DIR\"" >>"$profile_file"
}

# Modify the PATH environment variable.
modify_profile_path() {
  if [ "$MODIFY_PROFILE_PATH" = "yes" ]; then
    if [ "$VERBOSE" = "yes" ]; then
      echo "Modifying the PATH environment variable..."
    fi

    # Modify the PATH environment variable in the user's profile files.
    modify_profile_path_in_file "$HOME/.profile"
    modify_profile_path_in_file "$HOME/.bashrc"
    modify_profile_path_in_file "$HOME/.bash_profile"
    modify_profile_path_in_file "$HOME/.zshrc"
    modify_profile_path_in_file "$HOME/.zprofile"

    if [ "$QUIET" = "no" ]; then
      echo "Please restart your shell to finish the installation."
      echo "Alternatively, you can run the following command to apply the changes:"
      echo "  export PATH=\"\$PATH:$TAPA_INSTALL_DIR/usr/bin\""
    fi
  fi
}

main "$@"
