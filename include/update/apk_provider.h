#ifndef AURORA_UPDATE_APK_PROVIDER_H_
#define AURORA_UPDATE_APK_PROVIDER_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::update {

struct ProviderCheckFailure {
  std::string provider;
  std::string error;
};

struct ProviderVersion {
  std::string version_name;
  std::uint64_t version_code = 0;
  std::string error;
  // When a chain can identify one available source but other sources failed,
  // retain those failures so callers do not mistake partial coverage for a
  // complete check.
  std::vector<ProviderCheckFailure> check_failures;

  explicit operator bool() const { return error.empty(); }
};

struct ProviderDownloadResult {
  std::vector<std::filesystem::path> archives;
  // Provider that produced the archives, so a stored payload can be traced
  // back to where it came from.
  std::string source;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

// A source of Roblox APK archives. Providers are interchangeable, and every
// archive still passes the same signature, Build-ID, and ABI verification.
class ApkProvider {
 public:
  virtual ~ApkProvider() = default;

  // Short stable identifier, recorded in payload receipts and error messages.
  virtual std::string_view name() const = 0;

  // Latest published version. A provider that only serves pinned versions
  // reports an error here and stays usable through DownloadExact.
  virtual ProviderVersion CheckLatest() const = 0;

  virtual ProviderDownloadResult DownloadExact(
      std::string_view version, const std::filesystem::path& output_directory,
      int progress_fd) const = 0;
};

// Latest checks consult every provider and select the highest version code.
// Exact downloads try providers in order, so one unreachable provider does
// not fail the whole update.
class ProviderChain final : public ApkProvider {
 public:
  void Add(std::unique_ptr<ApkProvider> provider);
  bool empty() const { return providers_.empty(); }

  std::string_view name() const override { return "provider-chain"; }
  ProviderVersion CheckLatest() const override;
  std::vector<const ApkProvider*> ProvidersInOrder() const;
  ProviderDownloadResult DownloadExact(
      std::string_view version, const std::filesystem::path& output_directory,
      int progress_fd) const override;

 private:
  std::vector<std::unique_ptr<ApkProvider>> providers_;
};

}  // namespace aurora::update

#endif  // AURORA_UPDATE_APK_PROVIDER_H_
