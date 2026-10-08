#ifndef AURORA_COMPAT_BIONIC_DNS_RUNTIME_H_
#define AURORA_COMPAT_BIONIC_DNS_RUNTIME_H_

#include <netdb.h>
#include <sys/socket.h>

#include <cstdint>
#include <string_view>

namespace aurora {
namespace compat {

// Android x86-64 orders ai_canonname before ai_addr, unlike glibc. Keep the
// guest layout explicit so DNS results never cross the ABI boundary as a host
// addrinfo.
struct BionicAddressInfo {
  int ai_flags = 0;
  int ai_family = 0;
  int ai_socktype = 0;
  int ai_protocol = 0;
  socklen_t ai_addrlen = 0;
  char* ai_canonname = nullptr;
  sockaddr* ai_addr = nullptr;
  BionicAddressInfo* ai_next = nullptr;
};

constexpr int kBionicAddressInfoNameNotFound = 8;

// Matches only upload infrastructure embedded in the supported Roblox
// payloads (plus Backtrace's public service suffix). Ordinary Roblox API,
// asset, auth, and game-session hosts are outside this policy.
bool IsBlockedCrashReportUploadHost(std::string_view host) noexcept;

int BionicGetAddressInfo(const char* node, const char* service,
                         const BionicAddressInfo* hints,
                         BionicAddressInfo** result) noexcept;
void BionicFreeAddressInfo(BionicAddressInfo* info) noexcept;
hostent* BionicGetHostByName(const char* name) noexcept;

}  // namespace compat
}  // namespace aurora

extern "C" {

int aurora_bionic_getaddrinfo(
    const char* node, const char* service,
    const aurora::compat::BionicAddressInfo* hints,
    aurora::compat::BionicAddressInfo** result);
void aurora_bionic_freeaddrinfo(aurora::compat::BionicAddressInfo* info);
hostent* aurora_bionic_gethostbyname(const char* name);

}  // extern "C"

#endif  // AURORA_COMPAT_BIONIC_DNS_RUNTIME_H_
