#ifndef AURORA_COMPAT_BIONIC_PRCTL_RUNTIME_H_
#define AURORA_COMPAT_BIONIC_PRCTL_RUNTIME_H_

extern "C" {

int aurora_bionic_prctl(int option, unsigned long argument2,
                          unsigned long argument3, unsigned long argument4,
                          unsigned long argument5);

}  // extern "C"

#endif  // AURORA_COMPAT_BIONIC_PRCTL_RUNTIME_H_
