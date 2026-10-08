#ifndef AURORA_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_
#define AURORA_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_

#include <cstdint>

#include "aurora/audio/opensl_runtime_abi.h"

#if defined(__GNUC__) || defined(__clang__)
#define AURORA_OPENSL_EXPORT __attribute__((visibility("default")))
#else
#define AURORA_OPENSL_EXPORT
#endif

// Process-wide, content-free evidence from the production OpenSL boundary.
// Counters contain no audio samples, filenames, account data, or device names.
struct AuroraOpenSlRuntimeStats {
  std::uint64_t submitted_buffers;
  std::uint64_t consumed_buffers;
  std::uint64_t clean_player_shutdowns;
};

extern "C" {

#if !defined(AURORA_USE_SYSTEM_OPENSL_HEADERS)
extern AURORA_OPENSL_EXPORT const
    aurora::audio::opensl_abi::InterfaceId SL_IID_ANDROIDCONFIGURATION;
extern AURORA_OPENSL_EXPORT const
    aurora::audio::opensl_abi::InterfaceId SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
extern AURORA_OPENSL_EXPORT const
    aurora::audio::opensl_abi::InterfaceId SL_IID_BUFFERQUEUE;
extern AURORA_OPENSL_EXPORT const
    aurora::audio::opensl_abi::InterfaceId SL_IID_ENGINE;
extern AURORA_OPENSL_EXPORT const
    aurora::audio::opensl_abi::InterfaceId SL_IID_PLAY;
extern AURORA_OPENSL_EXPORT const
    aurora::audio::opensl_abi::InterfaceId SL_IID_RECORD;
extern AURORA_OPENSL_EXPORT const
    aurora::audio::opensl_abi::InterfaceId SL_IID_VOLUME;

AURORA_OPENSL_EXPORT aurora::audio::opensl_abi::Result
AURORA_OPENSL_API_ENTRY slCreateEngine(
    aurora::audio::opensl_abi::Object* engine,
    aurora::audio::opensl_abi::Uint32 num_options,
    const aurora::audio::opensl_abi::EngineOption* options,
    aurora::audio::opensl_abi::Uint32 num_interfaces,
    const aurora::audio::opensl_abi::InterfaceId* interface_ids,
    const aurora::audio::opensl_abi::Boolean* interface_required);
#endif

AURORA_OPENSL_EXPORT aurora::audio::opensl_abi::Result
auroraOpenSlGetRuntimeStats(AuroraOpenSlRuntimeStats* stats,
                              std::uint32_t stats_size);

}  // extern "C"

#endif  // AURORA_AUDIO_OPENSL_PLAYBACK_RUNTIME_H_
