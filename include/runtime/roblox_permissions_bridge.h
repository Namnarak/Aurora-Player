#ifndef AURORA_RUNTIME_ROBLOX_PERMISSIONS_BRIDGE_H_
#define AURORA_RUNTIME_ROBLOX_PERMISSIONS_BRIDGE_H_
#include "runtime/roblox_message_bus_request_bridge.h"

namespace aurora {
namespace runtime {
using RobloxPermissionsMessageBusSymbols = RobloxMessageBusSymbols;
using RobloxPermissionsMessageBusObjects = RobloxMessageBusObjects;

class RobloxPermissionsBridge final {
 public:
  RobloxPermissionsBridge(JniEnvironmentProvider environment,
                          RobloxPermissionsMessageBusSymbols symbols,
                          RobloxPermissionsMessageBusObjects objects,
                          bool microphone_enabled);
  ~RobloxPermissionsBridge();
  Status Initialize();
  Status Shutdown();

 private:
  std::unique_ptr<RobloxMessageBusRequestBridge> bridge_;
};
}  // namespace runtime
}  // namespace aurora
#endif
