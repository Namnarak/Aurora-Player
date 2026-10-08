#include "update/apk_provider.h"

#include <string>
#include <utility>

namespace aurora::update {
namespace {

void AppendFailure(std::string* aggregate, std::string_view provider,
                   std::string_view failure) {
  if (!aggregate->empty()) aggregate->append("; ");
  aggregate->append(provider);
  aggregate->append(": ");
  aggregate->append(failure);
}

}  // namespace

void ProviderChain::Add(std::unique_ptr<ApkProvider> provider) {
  if (provider != nullptr) providers_.push_back(std::move(provider));
}

ProviderVersion ProviderChain::CheckLatest() const {
  ProviderVersion result;
  if (providers_.empty()) {
    result.error = "no APK provider is configured";
    return result;
  }
  std::string failures;
  std::vector<ProviderCheckFailure> check_failures;
  bool found = false;
  bool conflict = false;
  for (const std::unique_ptr<ApkProvider>& provider : providers_) {
    const ProviderVersion version = provider->CheckLatest();
    if (!version) {
      AppendFailure(&failures, provider->name(), version.error);
      check_failures.push_back(
          {std::string(provider->name()), version.error});
      continue;
    }
    if (version.version_name.empty() || version.version_code == 0) {
      constexpr std::string_view kInvalidIdentity =
          "provider returned an invalid version identity";
      AppendFailure(&failures, provider->name(), kInvalidIdentity);
      check_failures.push_back(
          {std::string(provider->name()), std::string(kInvalidIdentity)});
      continue;
    }
    if (!found || version.version_code > result.version_code) {
      result = version;
      found = true;
      conflict = false;
    } else if (version.version_code == result.version_code &&
               version.version_name != result.version_name) {
      conflict = true;
    }
  }
  if (conflict) {
    result = {};
    result.check_failures = std::move(check_failures);
    result.error = "providers disagree on the version name for the highest "
                   "version code";
    return result;
  }
  if (found) {
    result.check_failures = std::move(check_failures);
  } else {
    result.error = std::move(failures);
    result.check_failures = std::move(check_failures);
  }
  return result;
}

std::vector<const ApkProvider*> ProviderChain::ProvidersInOrder() const {
  std::vector<const ApkProvider*> providers;
  providers.reserve(providers_.size());
  for (const std::unique_ptr<ApkProvider>& provider : providers_) {
    providers.push_back(provider.get());
  }
  return providers;
}

ProviderDownloadResult ProviderChain::DownloadExact(
    std::string_view version, const std::filesystem::path& output_directory,
    int progress_fd) const {
  ProviderDownloadResult result;
  if (providers_.empty()) {
    result.error = "no APK provider is configured";
    return result;
  }
  std::string failures;
  for (const std::unique_ptr<ApkProvider>& provider : providers_) {
    // Each provider gets its own directory: providers refuse to write into a
    // directory another one already populated, and a failed attempt must not
    // poison the next.
    ProviderDownloadResult downloaded = provider->DownloadExact(
        version, output_directory / provider->name(), progress_fd);
    if (downloaded) return downloaded;
    AppendFailure(&failures, provider->name(), downloaded.error);
  }
  result.error = std::move(failures);
  return result;
}

}  // namespace aurora::update
