#ifndef AURORA_COMPAT_BIONIC_PTHREAD_CREATE_RUNTIME_H_
#define AURORA_COMPAT_BIONIC_PTHREAD_CREATE_RUNTIME_H_

#include <pthread.h>
#include <sched.h>
#include <sys/types.h>

#include <cstddef>

namespace aurora::compat {

using NativeThreadInitializer = void (*)();

// Bionic reserves 32 KiB of its 1 MiB thread allocation for the signal stack.
inline constexpr size_t kBionicLp64DefaultThreadStackSize =
    (1U * 1024U * 1024U) - (32U * 1024U);
static_assert(kBionicLp64DefaultThreadStackSize == 1015808U);

// Retry floor when the host rejects the Bionic-sized stack.
inline constexpr size_t kBionicLp64FallbackThreadStackSize = 2U * 1024U * 1024U;

// nullptr restores host behavior.
void ConfigureBionicPthreadThreadInitializer(
    NativeThreadInitializer initializer) noexcept;

}  // namespace aurora::compat

struct AuroraBionicPthreadAttr;

// Rebuilds attributes instead of passing Android pthread_attr_t to glibc.
extern "C" int aurora_bionic_pthread_create(
    pthread_t* thread, const AuroraBionicPthreadAttr* attr,
    void* (*start_routine)(void*), void* argument);

// Guest real-time scheduling is denied to avoid the host RLIMIT_RTTIME watchdog.
extern "C" int aurora_bionic_pthread_setschedparam(
    pthread_t thread, int policy, const struct sched_param* parameters);
extern "C" int aurora_bionic_sched_setscheduler(
    pid_t tid, int policy, const struct sched_param* parameters);

#endif  // AURORA_COMPAT_BIONIC_PTHREAD_CREATE_RUNTIME_H_
