#ifndef AURORA_LEGACY_LEGACY_RUNTIME_H_
#define AURORA_LEGACY_LEGACY_RUNTIME_H_

#include <memory>
#include <string>
#include <utility>

#include "aurora/status.h"
#include "runtime/auth_runtime_composition.h"
#include "runtime/command_line.h"

namespace jnivm {
class VM;
}  // namespace jnivm

namespace aurora::legacy {

class RuntimeDependencies final {
 public:
  using ShutdownBeforePlatformCallback = Status (*)(jnivm::VM* vm);

  RuntimeDependencies() = default;
  explicit RuntimeDependencies(
      runtime::AuthRuntimeComposition composition,
      ShutdownBeforePlatformCallback shutdown_before_platform = nullptr,
      std::string browser_tracker_cookie = {})
      : jni_vm_(std::move(composition.jni_vm)),
        account_identity_(std::move(composition.account_identity)),
        roblox_credential_(std::move(composition.credential)),
        browser_tracker_cookie_(std::move(browser_tracker_cookie)),
        clear_persisted_web_view_cookie_(
            composition.rejected_credential_retired),
        shutdown_before_platform_(shutdown_before_platform) {}
  ~RuntimeDependencies() {
    if (jni_vm_ != nullptr && shutdown_before_platform_ != nullptr) {
      (void)shutdown_before_platform_(jni_vm_.get());
    }
  }

  RuntimeDependencies(const RuntimeDependencies&) = delete;
  RuntimeDependencies& operator=(const RuntimeDependencies&) = delete;
  RuntimeDependencies(RuntimeDependencies&& other) noexcept
      : jni_vm_(std::move(other.jni_vm_)),
        account_identity_(std::move(other.account_identity_)),
        roblox_credential_(std::move(other.roblox_credential_)),
        browser_tracker_cookie_(std::move(other.browser_tracker_cookie_)),
        clear_persisted_web_view_cookie_(std::exchange(
            other.clear_persisted_web_view_cookie_, false)),
        shutdown_before_platform_(
            std::exchange(other.shutdown_before_platform_, nullptr)) {}
  RuntimeDependencies& operator=(RuntimeDependencies&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (jni_vm_ != nullptr && shutdown_before_platform_ != nullptr) {
      (void)shutdown_before_platform_(jni_vm_.get());
    }
    jni_vm_ = std::move(other.jni_vm_);
    account_identity_ = std::move(other.account_identity_);
    roblox_credential_ = std::move(other.roblox_credential_);
    browser_tracker_cookie_ = std::move(other.browser_tracker_cookie_);
    clear_persisted_web_view_cookie_ =
        std::exchange(other.clear_persisted_web_view_cookie_, false);
    shutdown_before_platform_ =
        std::exchange(other.shutdown_before_platform_, nullptr);
    return *this;
  }

  const std::shared_ptr<jnivm::VM>& jni_vm() const { return jni_vm_; }
  const jnivm::RobloxAuthIdentity& account_identity() const {
    return account_identity_;
  }
  const runtime::SecureRobloxCredential& roblox_credential() const {
    return roblox_credential_;
  }
  const std::string& browser_tracker_cookie() const {
    return browser_tracker_cookie_;
  }
  bool clear_persisted_web_view_cookie() const {
    return clear_persisted_web_view_cookie_;
  }

  // Transitional teardown boundary. Runtime-owned subsystems release
  // SDL resources here after guest workers stop and before window shutdown.
  Status ShutdownBeforePlatform() {
    return shutdown_before_platform_ == nullptr
               ? Status::Ok()
               : shutdown_before_platform_(jni_vm_.get());
  }

 private:
  std::shared_ptr<jnivm::VM> jni_vm_;
  jnivm::RobloxAuthIdentity account_identity_;
  runtime::SecureRobloxCredential roblox_credential_;
  std::string browser_tracker_cookie_;
  bool clear_persisted_web_view_cookie_ = false;
  ShutdownBeforePlatformCallback shutdown_before_platform_ = nullptr;
};

// Transitional runtime kept for the reverse-engineered startup path.
int Run(const runtime::CommandLineOptions& options,
        RuntimeDependencies dependencies);

}  // namespace aurora::legacy

#endif  // AURORA_LEGACY_LEGACY_RUNTIME_H_
