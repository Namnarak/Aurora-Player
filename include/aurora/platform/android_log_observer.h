#ifndef AURORA_PLATFORM_ANDROID_LOG_OBSERVER_H_
#define AURORA_PLATFORM_ANDROID_LOG_OBSERVER_H_

// Observes the fully formatted payload written through Aurora's liblog
// adapter. The callback must not retain tag or message; both are borrowed for
// the duration of the call. Passing nullptr removes the current observer.
using AuroraAndroidLogObserver = void (*)(int priority, const char* tag,
                                            const char* message);

extern "C" void aurora_android_log_set_observer(
    AuroraAndroidLogObserver observer);

#endif  // AURORA_PLATFORM_ANDROID_LOG_OBSERVER_H_
