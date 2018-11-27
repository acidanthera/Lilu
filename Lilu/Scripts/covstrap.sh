#!/bin/bash

#
#  covstrap.sh
#  Lilu
#
#  Copyright © 2018 vit9696. All rights reserved.
#

#
#  This script is supposed to quickly bootstrap Coverity Scan environment for Travis CI
#  to be later used with Lilu and plugins.
#
#  Latest version available at:
#  https://raw.githubusercontent.com/acidanthera/Lilu/master/Lilu/Scripts/covstrap.sh
#
#  Example usage:
#  src=$(/usr/bin/curl -Lfs https://raw.githubusercontent.com/acidanthera/Lilu/master/Lilu/Scripts/covstrap.sh) && eval "$src" || exit 1
#

PROJECT_PATH="$(pwd)"
if [ $? -ne 0 ] || [ ! -d "${PROJECT_PATH}" ]; then
  echo "ERROR: Failed to determine working directory!"
  exit 1
fi

# Avoid conflicts with PATH overrides.
CHMOD="/bin/chmod"
CURL="/usr/bin/curl"
MKDIR="/bin/mkdir"
RM="/bin/rm"

TOOLS=(
  "${CHMOD}"
  "${CURL}"
  "${MKDIR}"
  "${RM}"
)

for tool in "${TOOLS[@]}"; do
  if [ ! -x "${tool}" ]; then
    echo "ERROR: Missing ${tool}!"
    exit 1
  fi
done

# Coverity compatibility tools
COV_TOOLS_URL="https://raw.githubusercontent.com/acidanthera/Lilu/master/Lilu/Scripts"
COV_TOOLS=(
  "cov-cc"
  "cov-cxx"
  "cov-csrutil"
)

COV_OVERRIDES=(
  "clang"
  "clang++"
  "gcc"
  "g++"
)

COV_OVERRIDES_TARGETS=(
  "cov-cc"
  "cov-cxx"
  "cov-cc"
  "cov-cxx"
)

COV_OVERRIDE_NUM="${#COV_OVERRIDES[@]}"

# Export override variables
export COVERITY_RESULTS_DIR="${PROJECT_PATH}/cov-int"
export COVERITY_TOOLS_DIR="${PROJECT_PATH}/cov-tools"

export COVERITY_CSRUTIL_PATH="${COVERITY_TOOLS_DIR}/cov-csrutil"
export CC="${COVERITY_TOOLS_DIR}/cov-cc"
export CXX="${COVERITY_TOOLS_DIR}/cov-cxx"

# Prepare directory structure
ret=0
"${RM}" -rf "${COVERITY_TOOLS_DIR}"
"${MKDIR}" "${COVERITY_TOOLS_DIR}" || ret=$?

if [ $ret -ne 0 ]; then
  echo "ERROR: Failed to create cov-tools directory ${COVERITY_TOOLS_DIR} with code ${ret}!"
  exit 1
fi

# Prepare tools
cd cov-tools || exit 1

# Download tools to override
for tool in "${COV_TOOLS[@]}"; do
  url="${COV_TOOLS_URL}/${tool}"
  "${CURL}" -LfsO "${url}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to download ${tool} with code ${ret}!"
    exit 1
  fi
  "${CHMOD}" a+x "${tool}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to chmod ${tool} with code ${ret}!"
    exit 1
  fi
done

# Generate compiler tools PATH overrides
for ((i=0; $i<$COV_OVERRIDE_NUM; i++)); do
  tool="${COV_OVERRIDES[$i]}"
  target="${COV_OVERRIDES_TARGETS[$i]}"
  echo "${target} \"\$@\"" > "${tool}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to generate ${tool} override to ${target} with code ${ret}!"
    exit 1
  fi
  "${CHMOD}" a+x "${tool}" || ret=$?
  if [ $ret -ne 0 ]; then
    echo "ERROR: Failed to chmod ${tool} with code ${ret}!"
    exit 1
  fi
done

# Done with tools
cd .. || exit 1

# Refresh PATH to apply overrides
export PATH="${COVERITY_TOOLS_DIR}:${PATH}"
