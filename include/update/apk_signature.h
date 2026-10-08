#ifndef AURORA_UPDATE_APK_SIGNATURE_H_
#define AURORA_UPDATE_APK_SIGNATURE_H_

#include <filesystem>
#include <string>
#include <vector>

namespace aurora::update {

struct ApkSignatureResult {
  std::vector<std::string> certificate_sha256;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

// Verifies an APK Signature Scheme v2/v3 signer, including the signed-data
// signature and APK chunked content digest, and returns signer certificate
// fingerprints. V1-only archives are rejected.
ApkSignatureResult VerifyApkSignature(const std::filesystem::path& apk_path);

}  // namespace aurora::update

#endif  // AURORA_UPDATE_APK_SIGNATURE_H_
