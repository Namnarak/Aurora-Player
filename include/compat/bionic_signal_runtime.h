#ifndef AURORA_COMPAT_BIONIC_SIGNAL_RUNTIME_H_
#define AURORA_COMPAT_BIONIC_SIGNAL_RUNTIME_H_

#include <signal.h>

#include <cstdint>

namespace aurora::compat {

// Android LP64 exposes a POSIX sigaction layout that differs
// from both the Linux kernel and glibc layouts.
struct BionicSigaction {
  int flags = 0;
  int padding = 0;
  union {
    void (*handler)(int);
    void (*action)(int, siginfo_t*, void*);
  } callback{};
  std::uint64_t mask = 0;
  void (*restorer)() = nullptr;
};

static_assert(sizeof(BionicSigaction) == 32,
              "Android x86-64 sigaction ABI changed");

int BionicSigactionCall(int signal_number, const BionicSigaction* action,
                        BionicSigaction* old_action) noexcept;

}  // namespace aurora::compat

extern "C" int aurora_bionic_sigaction(
    int signal_number, const aurora::compat::BionicSigaction* action,
    aurora::compat::BionicSigaction* old_action);

#endif  // AURORA_COMPAT_BIONIC_SIGNAL_RUNTIME_H_
