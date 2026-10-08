#ifndef AURORA_UPDATE_APKPURE_PROVIDER_H_
#define AURORA_UPDATE_APKPURE_PROVIDER_H_

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "update/apk_provider.h"

namespace aurora::update {

ProviderVersion ParseApkPureLatestMetadata(std::string_view metadata);

std::vector<std::string> ParseApkPureExactDownloadUrls(
    std::string_view metadata, std::string_view version,
    std::string* error = nullptr);

class ApkPureProvider final : public ApkProvider {
 public:
  std::string_view name() const override { return "apk-pure"; }

  ProviderVersion CheckLatest() const override;
  // Checks the newest Android release across ABIs, for status reporting only.
  // DownloadExact still uses the guest ABI and validates the package ABI.
  ProviderVersion CheckLatestAllAbis() const;

  ProviderDownloadResult DownloadExact(
      std::string_view version, const std::filesystem::path& output_directory,
      int progress_fd = -1) const override;
};

}  // namespace aurora::update

#endif  // AURORA_UPDATE_APKPURE_PROVIDER_H_
