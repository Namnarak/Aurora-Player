#ifndef AURORA_COMPAT_BIONIC_ATFORK_RUNTIME_H_
#define AURORA_COMPAT_BIONIC_ATFORK_RUNTIME_H_

namespace aurora::compat {

using BionicAtForkCallback = void (*)(void);

// POSIX cannot unregister by dso_handle, so registered guest callbacks must
// remain mapped for every later fork.
int RegisterBionicAtFork(BionicAtForkCallback prepare,
                         BionicAtForkCallback parent,
                         BionicAtForkCallback child, void* dso_handle) noexcept;

}  // namespace aurora::compat

extern "C" {

int aurora_bionic_register_atfork(
    aurora::compat::BionicAtForkCallback prepare,
    aurora::compat::BionicAtForkCallback parent,
    aurora::compat::BionicAtForkCallback child, void* dso_handle);

}  // extern "C"

#endif  // AURORA_COMPAT_BIONIC_ATFORK_RUNTIME_H_
