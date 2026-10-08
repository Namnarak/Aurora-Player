#!/usr/bin/env bash
set -Eeuo pipefail

root=${1:?repository root is required}
terms=${root}/TERMS.md
privacy=${root}/PRIVACY.md
readme=${root}/README.md
runtime_config=${root}/src/runtime/runtime_config.cc
crash_policy=${root}/src/runtime/crash_report_policy.cc
support_bundle=${root}/src/runtime/support_bundle.cc
browser_tracker=${root}/src/services/browser_tracker_service.cc

fail() {
  printf 'legal/privacy policy contract failed: %s\n' "$*" >&2
  exit 1
}

[[ -s ${terms} ]] || fail 'TERMS.md is missing or empty'
[[ -s ${privacy} ]] || fail 'PRIVACY.md is missing or empty'

grep -Fq 'not an exploit platform' "${terms}" ||
  fail 'Terms no longer state the compatibility/safety purpose'
grep -Fq 'does not replace the agreement between the user and' "${terms}" ||
  fail 'Terms no longer preserve Roblox terms'
grep -Fq 'concealing Aurora in order to evade a Roblox security or enforcement system' "${terms}" ||
  fail 'Terms no longer prohibit project-supported concealment/evasion'

grep -Fq 'No Aurora-operated analytics or behavioral telemetry service by default.' "${privacy}" ||
  fail 'privacy baseline no longer forbids default Aurora telemetry'
grep -Fq 'No automatic upload of Aurora support bundles.' "${privacy}" ||
  fail 'privacy baseline no longer keeps support bundles local'
grep -Fq 'No silent credential import from Sober, Vinegar, arbitrary browsers' "${privacy}" ||
  fail 'privacy baseline no longer protects cross-client credentials'
grep -Fq 'treated as a way to hide Linux' "${privacy}" ||
  fail 'privacy policy no longer distinguishes privacy from platform concealment'

grep -Fq '[Terms of Use](TERMS.md)' "${readme}" ||
  fail 'README does not expose Terms'
grep -Fq '[Privacy Policy](PRIVACY.md)' "${readme}" ||
  fail 'README does not expose Privacy Policy'

# These code-level defaults are part of the published privacy promise.
grep -Fq '"AURORA_DISCORD_RPC_ENABLED", false' "${runtime_config}" ||
  fail 'Discord RPC is no longer opt-in by default'
grep -Fq '{"DFIntCrashReportingHundredthsPercentage", "0"}' "${crash_policy}" ||
  fail 'mandatory crash-upload policy is missing'
grep -Fq 'AURORA_DISABLE_SUPPORT_BUNDLE' "${support_bundle}" ||
  fail 'local support-bundle control is missing'
grep -Fq 'browser-tracker-api/device/initialize' "${browser_tracker}" ||
  fail 'Roblox BrowserTracker behavior changed without a privacy review'

# Aurora support bundles must remain a local-file facility. If this source
# gains a direct HTTP client dependency, require an explicit privacy review.
if grep -Eq '#include .*http_client|CurlHttpClient|HttpRequest|curl_easy_' \
    "${support_bundle}"; then
  fail 'support-bundle implementation gained a network upload path'
fi
