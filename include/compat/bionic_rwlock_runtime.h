#ifndef AURORA_COMPAT_BIONIC_RWLOCK_RUNTIME_H_
#define AURORA_COMPAT_BIONIC_RWLOCK_RUNTIME_H_

#include <pthread.h>

// Bionic pthread_rwlock_t is a state word in guest storage. rdlock/wrlock/
// unlock use __atomic_* and futex on that word. Zero-initialized storage
// still locks.
extern "C" {

int aurora_bionic_pthread_rwlock_init(pthread_rwlock_t* rwlock,
                                        const pthread_rwlockattr_t* attr);
int aurora_bionic_pthread_rwlock_destroy(pthread_rwlock_t* rwlock);
int aurora_bionic_pthread_rwlock_rdlock(pthread_rwlock_t* rwlock);
int aurora_bionic_pthread_rwlock_wrlock(pthread_rwlock_t* rwlock);
int aurora_bionic_pthread_rwlock_unlock(pthread_rwlock_t* rwlock);

}  // extern "C"

#endif  // AURORA_COMPAT_BIONIC_RWLOCK_RUNTIME_H_
