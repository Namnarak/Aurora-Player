#ifndef AURORA_RUNTIME_CRASH_REPORT_POLICY_H_
#define AURORA_RUNTIME_CRASH_REPORT_POLICY_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace aurora {
namespace runtime {

// Disables Roblox crash uploads while preserving unrelated overrides.
bool MergeCrashReportClientSettingsOverrides(std::string_view base_json,
                                             std::string* merged_json,
                                             std::string* error);

// AppBridge accepts only the FastVariable subset of this policy.
bool MergeCrashReportFastFlagsOverrides(std::string_view base_json,
                                        std::string* merged_json,
                                        std::string* error);

// Routes FastVariables already present in the composed client-settings object
// to the native FastFlag preload channel. Explicit FastFlag payload entries
// take precedence over client-settings values.
bool ComposeNativeFastFlagPayload(std::string_view client_settings_json,
                                 std::string_view explicit_fast_flags_json,
                                 std::string* merged_json,
                                 std::size_t* client_flag_count,
                                 std::string* error);

}  // namespace runtime
}  // namespace aurora

#endif  // AURORA_RUNTIME_CRASH_REPORT_POLICY_H_
