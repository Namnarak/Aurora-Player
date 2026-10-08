#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
readonly output_root="${project_root}/packaging/icons/hicolor"
readonly icon_sizes=(16 22 24 32 36 48 64 72 96 128 192 256 512)

if ! command -v magick >/dev/null 2>&1; then
  printf 'ImageMagick 7 (magick) is required to regenerate desktop icons\n' >&2
  exit 1
fi

generate_icon_set() {
  local source_icon="$1"
  local icon_name="$2"

  [[ -f "${source_icon}" ]] || {
    printf 'Missing source icon: %s\n' "${source_icon}" >&2
    exit 1
  }

  for size in "${icon_sizes[@]}"; do
    destination="${output_root}/${size}x${size}/apps/${icon_name}.png"
    mkdir -p -- "$(dirname -- "${destination}")"
    magick -background none "${source_icon}" -alpha on -filter Lanczos \
      -resize "${size}x${size}" -depth 8 -strip "PNG32:${destination}"
  done

  printf 'Generated %d %s icons from %s\n' \
    "${#icon_sizes[@]}" "${icon_name}" "${source_icon}"
}

generate_icon_set \
  "${project_root}/packaging/aurora-logo.png" \
  "space.bigrat.aurora"
