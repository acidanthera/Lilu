#!/bin/bash

#
#  bootstrap.sh
#  Lilu
#
#  Copyright © 2018 vit9696. All rights reserved.
#

#
#  This script is supposed to quickly bootstrap Lilu SDK for plugin building.
#  Depending on BUILD_MODE variable (prebuilt or compiled) a prebuilt or
#  a compiled Lilu release will be bootstrapped in the working directory.
#
#  Latest version available at:
#  https://raw.githubusercontent.com/acidanthera/Lilu/master/Lilu/Scripts/bootstrap.sh
#
#  Example usage:
#  src=$(/usr/bin/curl -Lfs https://raw.githubusercontent.com/acidanthera/Lilu/master/Lilu/Scripts/bootstrap.sh) && eval "$src" || exit 1
#

REPO_PATH="acidanthera/Lilu"
SDK_PATH="Lilu.kext"
SDK_CHECK_PATH="${SDK_PATH}/Contents/Resources/Headers/kern_api.hpp"

PROJECT_PATH="$(pwd)"
if [ $? -ne 0 ] || [ ! -d "${PROJECT_PATH}" ]; then
  echo "ERROR: Failed to determine working directory!"
  exit 1
fi

# Avoid conflicts with PATH overrides.
CURL="/usr/bin/curl"
GIT="/usr/bin/git"
GREP="/usr/bin/grep"
MKDIR="/bin/mkdir"
MV="/bin/mv"
RM="/bin/rm"
SED="/usr/bin/sed"
UNAME="/usr/bin/uname"
UNZIP="/usr/bin/unzip"
UUIDGEN="/usr/bin/uuidgen"
XCODEBUILD="/usr/bin/xcodebuild"

TOOLS=(
  "${CURL}"
  "${GIT}"
  "${GREP}"
  "${MKDIR}"
  "${MV}"
  "${RM}"
  "${SED}"
  "${UNAME}"
  "${UNZIP}"
  "${UUIDGEN}"
  "${XCODEBUILD}"
)

for tool in "${TOOLS[@]}"; do
  if [ ! -x "${tool}" ]; then
    echo "ERROR: Missing ${tool}!"
    exit 1
  fi
done

# Prepare temporary directory to avoid conflicts with other scripts.
# Sets TMP_PATH.
prepare_environment() {
  local ret=0

  local sys=$("${UNAME}") || ret=$?
  if [ $ret -ne 0 ] || [ "$sys" != "Darwin" ]; then
    echo "ERROR: This script is only meant to be used on Darwin systems!"
    return 1
  fi

  if [ -e "${SDK_PATH}" ]; then
    echo "ERROR: Found existing SDK directory ${SDK_PATH}, aborting!"
    return 1
  fi

  local uuid=$("${UUIDGEN}") || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to generate temporary UUID with code ${ret}!"
    return 1
  fi

  TMP_PATH="/tmp/lilutmp.${uuid}"
  if [ -e "${TMP_PATH}" ]; then
    echo "ERROR: Found existing temporary directory ${TMP_PATH}, aborting!"
    return 1
  fi

  "${MKDIR}" "${TMP_PATH}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to create temporary directory ${TMP_PATH} with code ${ret}!"
    return 1
  fi

  cd "${TMP_PATH}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory ${TMP_PATH} with code ${ret}!"
    "${RM}" -rf "${TMP_PATH}"
    return 1
  fi

  return 0
}

# Install prebuilt SDK for release distribution.
install_prebuilt_sdk() {
  local ret=0

  echo "Installing prebuilt SDK..."

  echo "-> Obtaining release manifest..."

  echo "-> Cloning the latest version from master..."

  # This is a really ugly hack due to GitHub API rate limits.
  local url="https://github.com/${REPO_PATH}"
  "${GIT}" clone "${url}" -b "master" "tmp" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to clone repository with code ${ret}!"
    return 1
  fi

  echo "-> Obtaining the latest tag..."

  cd "tmp" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory tmp with code ${ret}!"
    return 1
  fi

  local vers=$("${GIT}" describe --abbrev=0 --tags) || ret=$?
  if [ "$vers" = "" ]; then
    echo "ERROR: Failed to determine the latest release tag!"
    return 1
  fi

  echo "-> Discovered the latest tag ${vers}."

  cd .. || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd back from temporary directory with code ${ret}!"
    return 1
  fi

  "${RM}" -rf tmp || ret=$?
  if [ $ret -ne 0 ] || [ -d tmp ]; then
    echo "ERROR: Failed to remove temporary directory tmp with code ${ret}!"
    return 1
  fi

  local file="Lilu-${vers}-DEBUG.zip"

  echo "-> Downloading prebuilt debug version ${file}..."

  local url="https://github.com/${REPO_PATH}/releases/download/${vers}/${file}"
  "${CURL}" -LfsO "${url}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to download ${file} with code ${ret}!"
    return 1
  fi

  echo "-> Extracting SDK from prebuilt debug version..."

  if [ ! -f "${file}" ]; then
    echo "ERROR: Failed to download ${file} to a non-existent location!"
    return 1
  fi

  "${MKDIR}" "tmp" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to create temporary directory at ${TMP_PATH} with code ${ret}!"
    return 1
  fi

  cd "tmp" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory tmp with code ${ret}!"
    return 1
  fi

  "${UNZIP}" -q ../"${file}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to unzip ${file} with code ${ret}!"
    return 1
  fi

  echo "-> Installing SDK from the prebuilt debug version..."

  if [ ! -d "${SDK_PATH}" ] || [ ! -f "${SDK_CHECK_PATH}" ]; then
    echo "ERROR: Failed to find SDK in the downloaded archive!"
    return 1
  fi

  "${MV}" "${SDK_PATH}" "${PROJECT_PATH}/${SDK_PATH}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to install SDK with code ${ret}!"
    return 1
  fi

  echo "Installed prebuilt SDK ${vers}!"

  return 0
}

# Install manually compiled SDK for development builds.
install_compiled_sdk() {
  local ret=0

  echo "Installing compiled SDK..."

  echo "-> Cloning the latest version from master..."

  local url="https://github.com/${REPO_PATH}"
  "${GIT}" clone "${url}" -b "master" --depth=1 "tmp" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to clone repository with code ${ret}!"
    return 1
  fi

  echo "-> Building the latest SDK..."

  cd "tmp" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to cd to temporary directory tmp with code ${ret}!"
    return 1
  fi

  "${GIT}" clone "https://github.com/acidanthera/MacKernelSDK" -b "master" --depth=1 || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to clone MacKernelSDK with code ${ret}!"
    return 1
  fi

  if [ -n "${ACID32}" ]; then
    echo "-> ACID32 specified, installing clang32..."
    src=$("${CURL}" -Lfs https://raw.githubusercontent.com/acidanthera/ocbuild/master/clang32-bootstrap.sh) && eval "$src" || ret=$?

    if [ $ret -ne 0 ]; then
      echo "ERROR: Failed to install clang32 with code ${ret}!"
      return 1
    fi
  fi

  if [ -n "${ACID32}" ]; then
    "${XCODEBUILD}" -configuration Debug -arch ACID32 -arch x86_64 || ret=$?
  else
    "${XCODEBUILD}" -configuration Debug -arch x86_64 || ret=$?
  fi

  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to compile the latest version with code ${ret}!"
    return 1
  fi

  echo "-> Installing compiled SDK..."

  if [ ! -d "build/Debug/${SDK_PATH}" ] || [ ! -f "build/Debug/${SDK_CHECK_PATH}" ]; then
    echo "ERROR: Failed to find the built SDK!"
    return 1
  fi

  "${MV}" "build/Debug/${SDK_PATH}" "${PROJECT_PATH}/${SDK_PATH}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to install SDK with code ${ret}!"
    return 1
  fi

  echo "Installed compiled SDK from master!"
}

prepare_environment || exit 1

ret=0
if [ "${TRAVIS_TAG}" != "" ] || [[ "${GITHUB_REF}" = refs/tags/* ]] || [ "${BUILD_MODE}" = "prebuilt" ]; then
  install_prebuilt_sdk || ret=$?
else
  install_compiled_sdk || ret=$?
fi

cd "${PROJECT_PATH}" || ret=$?

"${RM}" -rf "${TMP_PATH}"

if [ $ret -ne 0 ]; then
  echo "ERROR: Failed to bootstrap SDK with code ${ret}!"
  exit 1
fi
