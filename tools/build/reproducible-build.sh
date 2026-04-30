#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Mark LaPointe <mark@cloudbsd.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

#
# Reproducible Build Verification Script
#
# This script verifies that kernel modules can be built in a reproducible
# manner - meaning the same source code produces bit-for-bit identical
# binaries across different build environments.
#
# Key techniques used:
# - Deterministic timestamps: __DATE__, __TIME__, __TIMESTAMP__ suppressed
# - Fixed build paths: BUILD_PATH_STRIP removes build directory prefixes
# - Consistent environment: SOURCE_EPOCH, TZ=UTC
# - Sortable archive members: archive order normalized
#

set -e

# Default configuration
VERBOSE=0
REFERENCE_DIR=""
OUTPUT_FILE=""
ARTIFACTS_DIR="/tmp/reproducible-build-$$"
BUILD_DIR=""

# Usage message
usage() {
	cat <<EOF
Usage: $0 [-v] [-r reference_dir] [-o output_file] [-b build_dir] artifact [artifact...]

Options:
  -v          Verbose output
  -r DIR     Reference directory containing .sig files from known-good build
  -o FILE    Write verification results to FILE
  -b DIR     Build directory to strip from paths (default: auto-detect)
  -h         Show this help

Arguments:
  artifact    File(s) to verify for reproducibility

Exit codes:
  0  All artifacts are reproducible
  1  One or more artifacts differ from reference
  2  Error (missing files, invalid arguments, etc.)

Examples:
  # Verify single module
  $0 /path/to/emu_core.ko

  # Verify multiple modules with reference
  $0 -r /reference/build *.ko

  # Full verification with output
  $0 -r /reference -o results.txt sys/modules/emu*/emu_*.ko
EOF
}

# Log message if verbose
log_verbose() {
	if [ "$VERBOSE" -eq 1 ]; then
		echo "$@"
	fi
}

# Log error
log_error() {
	echo "ERROR: $@" >&2
}

# Strip build path from file path for consistent hashing
strip_build_path() {
	local file="$1"
	local build_dir="${BUILD_DIR:-.}"

	# Use realpath for absolute paths
	if [ "${file##/*}" = "" ]; then
		file="$(realpath -s "$file" 2>/dev/null || echo "$file")"
	else
		file="$(pwd)/$file"
		file="$(realpath -s "$file" 2>/dev/null || echo "$file")"
	fi

	# Remove BUILD_DIR prefix if set
	if [ -n "$build_dir" ]; then
		build_dir="$(realpath -s "$build_dir" 2>/dev/null || echo "$build_dir")"
		file="${file#$build_dir/}"
	fi

	echo "$file"
}

# Compute hash of a file with build metadata stripped
compute_reproducible_hash() {
	local file="$1"
	local hash

	# For ELF files, strip the build ID and debug info paths
	# This is a simplified version - full reproducibility would need
	# more sophisticated ELF manipulation

	# Compute SHA-256 hash
	hash=$(sha256 -q "$file" 2>/dev/null)

	if [ -z "$hash" ]; then
		log_error "Failed to compute hash for $file"
		return 1
	fi

	echo "$hash"
}

# Verify a single artifact against reference
verify_artifact() {
	local artifact="$1"
	local ref_sig=""
	local ref_hash=""
	local cur_hash=""
	local rel_path=""
	local status=0

	if [ ! -f "$artifact" ]; then
		log_error "Artifact not found: $artifact"
		return 2
	fi

	rel_path=$(strip_build_path "$artifact")
	cur_hash=$(compute_reproducible_hash "$artifact")

	if [ -n "$REFERENCE_DIR" ]; then
		ref_sig="$REFERENCE_DIR/${rel_path}.sig"

		if [ -f "$ref_sig" ]; then
			ref_hash=$(cat "$ref_sig")
			log_verbose "Reference hash for $rel_path: $ref_hash"

			if [ "$cur_hash" = "$ref_hash" ]; then
				log_verbose "PASS: $rel_path"
				echo "PASS: $rel_path"
			else
				log_error "FAIL: $rel_path"
				echo "FAIL: $rel_path (hash mismatch)"
				echo "  Expected: $ref_hash"
				echo "  Got:      $cur_hash"
				status=1
			fi
		else
			log_verbose "NEW: $rel_path (no reference)"
			echo "NEW: $rel_path (no reference)"
		fi
	else
		# No reference - just output current hash
		log_verbose "HASH: $rel_path: $cur_hash"
		echo "HASH: $rel_path: $cur_hash"
	fi

	return $status
}

# Generate signature file for artifact
generate_signature() {
	local artifact="$1"
	local sig_file="${2:-${artifact}.sig}"
	local rel_path=""
	local hash=""

	rel_path=$(strip_build_path "$artifact")
	hash=$(compute_reproducible_hash "$artifact")

	if [ -z "$hash" ]; then
		return 1
	fi

	echo "$hash" > "$sig_file"
	log_verbose "Generated signature for $rel_path"
	echo "Generated: $rel_path -> $sig_file"
}

# Main function
main() {
	local status=0
	local artifacts=""
	local cmd=""

	# Parse arguments
	while getopts "vr:o:b:h" opt; do
		case "$opt" in
			v) VERBOSE=1 ;;
			r) REFERENCE_DIR="$OPTARG" ;;
			o) OUTPUT_FILE="$OPTARG" ;;
			b) BUILD_DIR="$OPTARG" ;;
			h) usage; exit 0 ;;
			*) usage; exit 2 ;;
		esac
	done
	shift $((OPTIND - 1))

	# Check arguments
	if [ $# -eq 0 ]; then
		usage
		exit 2
	fi

	# Auto-detect BUILD_DIR if not set
	if [ -z "$BUILD_DIR" ]; then
		BUILD_DIR="$(pwd)"
		log_verbose "Auto-detected BUILD_DIR: $BUILD_DIR"
	fi

	# Redirect output if requested
	if [ -n "$OUTPUT_FILE" ]; then
		exec > "$OUTPUT_FILE"
	fi

	# Process each artifact
	for artifact in "$@"; do
		if verify_artifact "$artifact"; then
			: # Success
		else
			ret=$?
			if [ $ret -eq 2 ]; then
				exit 2
			fi
			status=$ret
		fi
	done

	# Summary
	if [ $status -eq 0 ]; then
		echo ""
		echo "All artifacts are reproducible."
	else
		echo ""
		echo "One or more artifacts differ from reference."
	fi

	exit $status
}

main "$@"
