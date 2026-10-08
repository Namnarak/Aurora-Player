#ifndef AURORA_RUNTIME_SUPPORTED_LAUNCH_POLICY_H_
#define AURORA_RUNTIME_SUPPORTED_LAUNCH_POLICY_H_

#include <string>

namespace aurora {
namespace runtime {

// Publishes relocatable installed-resource paths and backend-independent
// interactive LuaApp defaults. Graphics policy is applied only after the
// runtime configuration has been resolved.
bool ApplySupportedLaunchPolicy(bool interactive, std::string* error = nullptr);

}  // namespace runtime
}  // namespace aurora

#endif  // AURORA_RUNTIME_SUPPORTED_LAUNCH_POLICY_H_
