// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#ifndef AURORA_UPDATE_APKCOMBO_PROVIDER_H_
#define AURORA_UPDATE_APKCOMBO_PROVIDER_H_

#include <filesystem>
#include <string_view>

#include "update/apk_provider.h"

namespace aurora::update {

class ApkComboProvider final : public ApkProvider {
 public:
  std::string_view name() const override { return "apkcombo"; }

  ProviderVersion CheckLatest() const override;
  ProviderDownloadResult DownloadExact(
      std::string_view version, const std::filesystem::path& output_directory,
      int progress_fd = -1) const override;
};

}  // namespace aurora::update

#endif  // AURORA_UPDATE_APKCOMBO_PROVIDER_H_
