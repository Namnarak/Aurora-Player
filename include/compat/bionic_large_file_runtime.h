#ifndef AURORA_COMPAT_BIONIC_LARGE_FILE_RUNTIME_H_
#define AURORA_COMPAT_BIONIC_LARGE_FILE_RUNTIME_H_

#include <sys/types.h>

#include <cstddef>
#include <cstdint>

namespace aurora::compat {

// Android x86-64 defines off64_t as a signed 64-bit offset. A fixed-width
// public type keeps the guest ABI independent of host feature-test macros.
using BionicOff64 = int64_t;

ssize_t BionicPread64(int fd, void* buffer, size_t count,
                      BionicOff64 offset) noexcept;
ssize_t BionicPwrite64(int fd, const void* buffer, size_t count,
                       BionicOff64 offset) noexcept;
BionicOff64 BionicLseek64(int fd, BionicOff64 offset, int whence) noexcept;

}  // namespace aurora::compat

extern "C" {

ssize_t aurora_bionic_pread64(int fd, void* buffer, size_t count,
                                aurora::compat::BionicOff64 offset);
ssize_t aurora_bionic_pwrite64(int fd, const void* buffer, size_t count,
                                 aurora::compat::BionicOff64 offset);
aurora::compat::BionicOff64
aurora_bionic_lseek64(int fd, aurora::compat::BionicOff64 offset,
                        int whence);

}  // extern "C"

#endif  // AURORA_COMPAT_BIONIC_LARGE_FILE_RUNTIME_H_
