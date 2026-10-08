#ifndef AURORA_AUDIO_FMOD_JNI_AUDIO_BRIDGE_H_
#define AURORA_AUDIO_FMOD_JNI_AUDIO_BRIDGE_H_

#include "aurora/status.h"

namespace jnivm {
class VM;
}  // namespace jnivm

namespace aurora::audio {

// Installs the SDL-backed org/fmod/AudioDevice implementation into the
// pseudo-JVM. The VM retains and shuts down the bridge context.
Status InstallFmodJniAudioBridge(jnivm::VM* vm);

// Stops the Java audio runtime and every SDL stream before the window layer
// calls SDL_Quit(). Must run on the main thread after Roblox workers stop.
Status ShutdownFmodJniAudioBridge(jnivm::VM* vm);

}  // namespace aurora::audio

#endif  // AURORA_AUDIO_FMOD_JNI_AUDIO_BRIDGE_H_
