#ifndef AURORA_RUNTIME_PROCESS_DIAGNOSTICS_H_
#define AURORA_RUNTIME_PROCESS_DIAGNOSTICS_H_

namespace aurora::runtime {

enum class ProcessDiagnosticStage { kStartup, kNativeRuntime, kShutdown };

// Installs a diagnostic handler only if SIGXCPU still has its default action.
// The handler logs to stderr and re-delivers SIGXCPU with the default action.
void InstallCpuLimitDiagnostics();
void LogProcessDiagnostics(ProcessDiagnosticStage stage);

}  // namespace aurora::runtime

#endif  // AURORA_RUNTIME_PROCESS_DIAGNOSTICS_H_
