#!/usr/bin/env bash
# Copyright 2026 Aurora Project Authors
# SPDX-License-Identifier: Apache-2.0

set -Eeuo pipefail
umask 022

readonly STABLE_DEB="${1:?Stable DEB path is required}"
readonly NIGHTLY_DEB="${2:?Nightly DEB path is required}"
readonly STABLE_RPM="${3:?Stable RPM path is required}"
readonly NIGHTLY_RPM="${4:?Nightly RPM path is required}"
readonly PUBLIC_KEY="${5:?GPG public key path is required}"
readonly GPG_KEY_ID="${6:?GPG key ID is required}"
readonly GPG_HOME="${7:?GPG home path is required}"
readonly OUTPUT="${8:?Repository output path is required}"
readonly BASE_URL="${AURORA_REPO_BASE_URL:?Set AURORA_REPO_BASE_URL to your public HTTPS repository base URL}"
[[ "$BASE_URL" == https://* && "$BASE_URL" != */ ]] || {
  echo "AURORA_REPO_BASE_URL must be HTTPS without a trailing slash" >&2
  exit 1
}

for command in apt-ftparchive createrepo_c dpkg-deb gpg gzip rpm rpmsign; do
  command -v "${command}" >/dev/null || {
    printf 'Required command is missing: %s\n' "${command}" >&2
    exit 1
  }
done

for package in "${STABLE_DEB}" "${NIGHTLY_DEB}" \
               "${STABLE_RPM}" "${NIGHTLY_RPM}"; do
  [[ -s "${package}" && ! -L "${package}" ]] || {
    printf 'Package is missing or unsafe: %s\n' "${package}" >&2
    exit 1
  }
done

[[ -s "${PUBLIC_KEY}" && ! -L "${PUBLIC_KEY}" ]] || {
  printf 'GPG public key is missing or unsafe: %s\n' "${PUBLIC_KEY}" >&2
  exit 1
}
[[ -d "${GPG_HOME}" && ! -L "${GPG_HOME}" ]] || {
  printf 'GPG home is missing or unsafe: %s\n' "${GPG_HOME}" >&2
  exit 1
}
[[ ! -e "${OUTPUT}" ]] || {
  printf 'Repository output already exists: %s\n' "${OUTPUT}" >&2
  exit 1
}

[[ "$(dpkg-deb --field "${STABLE_DEB}" Package)" == aurora-player ]]
[[ "$(dpkg-deb --field "${NIGHTLY_DEB}" Package)" == aurora-player-nightly ]]
[[ "$(rpm --query --package --queryformat '%{NAME}' "${STABLE_RPM}")" == aurora-player ]]
[[ "$(rpm --query --package --queryformat '%{NAME}' "${NIGHTLY_RPM}")" == aurora-player-nightly ]]

readonly APT_ROOT="${OUTPUT}/apt"
readonly APT_POOL="${APT_ROOT}/pool/main/m/aurora"
readonly APT_DIST="${APT_ROOT}/dists/aurora"
readonly RPM_ROOT="${OUTPUT}/rpm/x86_64"
readonly DOWNLOADS="${OUTPUT}/downloads"

install -d -- \
  "${APT_POOL}" "${APT_DIST}/main/binary-amd64" "${RPM_ROOT}" "${DOWNLOADS}"
install -m 0644 -- "${STABLE_DEB}" "${APT_POOL}/$(basename -- "${STABLE_DEB}")"
install -m 0644 -- "${NIGHTLY_DEB}" "${APT_POOL}/$(basename -- "${NIGHTLY_DEB}")"
install -m 0644 -- "${STABLE_RPM}" "${RPM_ROOT}/$(basename -- "${STABLE_RPM}")"
install -m 0644 -- "${NIGHTLY_RPM}" "${RPM_ROOT}/$(basename -- "${NIGHTLY_RPM}")"
install -m 0644 -- "${STABLE_DEB}" "${DOWNLOADS}/aurora.deb"
install -m 0644 -- "${STABLE_RPM}" "${DOWNLOADS}/aurora.rpm"
install -m 0644 -- "${PUBLIC_KEY}" "${OUTPUT}/aurora-packages.gpg"

(
  cd -- "${APT_ROOT}"
  apt-ftparchive packages pool/main >dists/aurora/main/binary-amd64/Packages
  gzip -9n -c dists/aurora/main/binary-amd64/Packages \
    >dists/aurora/main/binary-amd64/Packages.gz
  apt-ftparchive \
    -o APT::FTPArchive::Release::Origin=Aurora \
    -o APT::FTPArchive::Release::Label=Aurora \
    -o APT::FTPArchive::Release::Suite=aurora \
    -o APT::FTPArchive::Release::Codename=aurora \
    -o APT::FTPArchive::Release::Architectures=amd64 \
    -o APT::FTPArchive::Release::Components=main \
    release dists/aurora >dists/aurora/Release
)

gpg --batch --yes --homedir "${GPG_HOME}" --local-user "${GPG_KEY_ID}" \
  --digest-algo SHA256 --clearsign \
  --output "${APT_DIST}/InRelease" "${APT_DIST}/Release"
gpg --batch --yes --homedir "${GPG_HOME}" --local-user "${GPG_KEY_ID}" \
  --digest-algo SHA256 --armor --detach-sign \
  --output "${APT_DIST}/Release.gpg" "${APT_DIST}/Release"

for package in "${RPM_ROOT}"/*.rpm; do
  GNUPGHOME="${GPG_HOME}" rpmsign --addsign \
    --define "_gpg_name ${GPG_KEY_ID}" \
    --define "_gpg_path ${GPG_HOME}" \
    "${package}"
done

createrepo_c "${RPM_ROOT}"
gpg --batch --yes --homedir "${GPG_HOME}" --local-user "${GPG_KEY_ID}" \
  --digest-algo SHA256 --armor --detach-sign \
  --output "${RPM_ROOT}/repodata/repomd.xml.asc" \
  "${RPM_ROOT}/repodata/repomd.xml"

cat >"${OUTPUT}/rpm/aurora.repo" <<EOF
[aurora]
name=Aurora
baseurl=${BASE_URL}/rpm/\$basearch/
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=${BASE_URL}/aurora-packages.gpg
EOF
