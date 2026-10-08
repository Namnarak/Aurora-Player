#ifndef AURORA_RUNTIME_FAILURE_DIALOG_H_
#define AURORA_RUNTIME_FAILURE_DIALOG_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "runtime/environment.h"

namespace aurora {
namespace runtime {

// Canaries and headless sessions remain non-interactive.
bool FailureDialogsEnabled(const Environment& environment);

bool ShowFailureDialog(const Environment& environment,
                       std::string_view message);
bool ShowAlreadyRunningDialog(const Environment& environment,
                              std::string_view message);
bool ShowWarningDialog(const Environment& environment,
                       std::string_view message);

// A separate Adwaita helper owns the dialog. Poll from the runtime event
// loop so a user's choice never blocks SDL input and window processing.
class ChoiceDialog final {
 public:
  ChoiceDialog() = default;
  ~ChoiceDialog();

  ChoiceDialog(const ChoiceDialog&) = delete;
  ChoiceDialog& operator=(const ChoiceDialog&) = delete;
  ChoiceDialog(ChoiceDialog&& other);
  ChoiceDialog& operator=(ChoiceDialog&& other);

  static ChoiceDialog Start(const Environment& environment,
                            std::string_view heading,
                            std::string_view message,
                            std::string_view first_response,
                            std::string_view second_response,
                            std::string_view third_response,
                            std::uint8_t enabled_responses = 0x07);

  bool active() const { return socket_ >= 0; }
  // Returns response index 0, 1, or 2 when the dialog is closed. Dismissal
  // and helper failures resolve to response 2, the safe/close choice.
  std::optional<int> Poll();
  void Dismiss();

 private:
  ChoiceDialog(int socket, int helper_pid);
  void ReapHelpers();

  int socket_ = -1;
  std::vector<int> helper_pids_;
  std::optional<int> response_;
};

// The helper starts before guest threads, so it can report errors and fatal
// exits even when the main process can no longer show a dialog.
class FailureDialogMonitor final {
 public:
  FailureDialogMonitor() = default;
  ~FailureDialogMonitor();

  FailureDialogMonitor(const FailureDialogMonitor&) = delete;
  FailureDialogMonitor& operator=(const FailureDialogMonitor&) = delete;
  FailureDialogMonitor(FailureDialogMonitor&& other) noexcept;
  FailureDialogMonitor& operator=(FailureDialogMonitor&& other) noexcept;

  static FailureDialogMonitor Start(const Environment& environment,
                                    std::string_view initial_message);

  bool active() const { return socket_ >= 0; }
  void SetMessage(std::string_view message);
  void MarkSuccessful();

 private:
  FailureDialogMonitor(int socket, int helper_pid);
  void Finish(char disposition);

  int socket_ = -1;
  int helper_pid_ = -1;
};

}  // namespace runtime
}  // namespace aurora

#endif  // AURORA_RUNTIME_FAILURE_DIALOG_H_
