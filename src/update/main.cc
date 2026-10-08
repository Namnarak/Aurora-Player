#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <cerrno>

#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "update/android_manifest.h"
#include "update/apk_bundle.h"
#include "update/apk_signature.h"
#include "update/compatibility_catalog.h"
#include "update/host_abi_deriver.h"
#include "update/payload_store.h"
#include "update/update_coordinator.h"
#include "update/zip_archive.h"

#ifndef AURORA_DEFAULT_COMPATIBILITY_MANIFEST
#define AURORA_DEFAULT_COMPATIBILITY_MANIFEST \
  "config/roblox_compatibility.json"
#endif

#ifndef AURORA_DEFAULT_SIGNING_TRUST_MANIFEST
#define AURORA_DEFAULT_SIGNING_TRUST_MANIFEST \
  "config/roblox_signing_certificates.json"
#endif

namespace {

std::optional<std::string> Environment(std::string_view name) {
  const char* value = std::getenv(std::string(name).c_str());
  if (value == nullptr || value[0] == '\0') return std::nullopt;
  return value;
}

std::filesystem::path XdgRoot(std::string_view variable,
                              std::string_view fallback_suffix) {
  const auto configured = Environment(variable);
  if (configured.has_value() &&
      std::filesystem::path(*configured).is_absolute()) {
    return *configured;
  }
  return std::filesystem::path(Environment("HOME").value_or("/root")) /
         std::string(fallback_suffix);
}

std::filesystem::path ExecutablePath() {
  std::array<char, 4097> path{};
  const ssize_t size =
      readlink("/proc/self/exe", path.data(), path.size() - 1U);
  if (size <= 0 || static_cast<std::size_t>(size) >= path.size()) return {};
  path[static_cast<std::size_t>(size)] = '\0';
  return path.data();
}

std::filesystem::path InstalledMetadataEntry(std::string_view name,
                                             bool directory_entry) {
  const std::filesystem::path executable = ExecutablePath();
  if (!executable.empty()) {
    const std::filesystem::path directory = executable.parent_path();
    if (directory.filename() == "aurora" &&
        directory.parent_path().filename() == "lib") {
      const std::filesystem::path candidate =
          directory.parent_path().parent_path() / "share/aurora/metadata" /
          std::string(name);
      std::error_code error;
      if (directory_entry ? std::filesystem::is_directory(candidate, error)
                          : std::filesystem::is_regular_file(candidate, error)) {
        return candidate;
      }
    }
  }
  return {};
}

std::filesystem::path InstalledMetadata(std::string_view filename) {
  return InstalledMetadataEntry(filename, false);
}

// A directory of per-Build-ID sidecars wins over the single legacy file.
std::filesystem::path DefaultHostAbiReference() {
  const std::filesystem::path installed_directory =
      InstalledMetadataEntry("host_abi", true);
  if (!installed_directory.empty()) return installed_directory;
  const std::filesystem::path installed_file =
      InstalledMetadata("roblox_host_abi_reference.json");
  if (!installed_file.empty()) return installed_file;
  std::error_code error;
  if (std::filesystem::is_directory("config/host_abi", error)) {
    return "config/host_abi";
  }
  return "config/roblox_host_abi_reference.json";
}

aurora::update::UpdatePaths ResolvePaths() {
  aurora::update::UpdatePaths paths;
  paths.config_file = Environment("AURORA_CONFIG_FILE")
                          .value_or((XdgRoot("XDG_CONFIG_HOME", ".config") /
                                     "aurora/config.yaml")
                                        .string());
  paths.data_root =
      Environment("AURORA_DATA_ROOT")
          .value_or(
              (XdgRoot("XDG_DATA_HOME", ".local/share") / "aurora").string());
  paths.cache_root =
      Environment("AURORA_CACHE_ROOT")
          .value_or(
              (XdgRoot("XDG_CACHE_HOME", ".cache") / "aurora").string());
  paths.state_root =
      Environment("AURORA_STATE_ROOT")
          .value_or((XdgRoot("XDG_STATE_HOME", ".local/state") / "aurora")
                        .string());
  const std::filesystem::path installed_compatibility =
      InstalledMetadata("roblox_compatibility.json");
  const std::filesystem::path installed_trust =
      InstalledMetadata("roblox_signing_certificates.json");
  const std::filesystem::path default_host_abi_reference =
      DefaultHostAbiReference();
  paths.compatibility_manifest =
      Environment("AURORA_UPDATE_COMPATIBILITY_PATH")
          .value_or(installed_compatibility.empty()
                        ? AURORA_DEFAULT_COMPATIBILITY_MANIFEST
                        : installed_compatibility.string());
  paths.signing_trust_manifest =
      Environment("AURORA_UPDATE_SIGNING_TRUST_PATH")
          .value_or(installed_trust.empty()
                        ? AURORA_DEFAULT_SIGNING_TRUST_MANIFEST
                        : installed_trust.string());
  paths.host_abi_reference_profile =
      Environment("AURORA_UPDATE_HOST_ABI_REFERENCE")
          .value_or(default_host_abi_reference.string());
  const std::filesystem::path executable = ExecutablePath();
  std::filesystem::path runtime = executable.parent_path() / "aurora";
  if (executable.parent_path().filename() == "aurora" &&
      executable.parent_path().parent_path().filename() == "lib") {
    const std::filesystem::path public_runtime =
        executable.parent_path().parent_path().parent_path() / "bin/aurora";
    std::error_code error;
    if (std::filesystem::is_regular_file(public_runtime, error)) {
      runtime = public_runtime;
    }
  }
  paths.runtime_binary =
      Environment("AURORA_UPDATE_CANARY_BIN").value_or(runtime.string());
  return paths;
}

int ProgressDescriptor() {
  const auto value = Environment("AURORA_UPDATE_PROGRESS_FD");
  if (!value.has_value()) return -1;
  int descriptor = -1;
  const auto parsed =
      std::from_chars(value->data(), value->data() + value->size(), descriptor);
  return parsed.ec == std::errc() &&
                 parsed.ptr == value->data() + value->size() && descriptor >= 0
             ? descriptor
             : -1;
}

// Internal handoff from Aurora. FD 10 refers to the exact runtime process;
// FD 11 shares its existing instance lock, continuously excluding a relaunch.
// Waiting must succeed before any call capable of promoting a payload.
bool WaitForRuntimeExit() {
  int socket_type = 0;
  socklen_t socket_type_size = sizeof(socket_type);
  sockaddr_storage socket_address{};
  socklen_t socket_address_size = sizeof(socket_address);
  const bool readiness_socket_valid =
      getsockopt(12, SOL_SOCKET, SO_TYPE, &socket_type, &socket_type_size) == 0 &&
      socket_type == SOCK_SEQPACKET &&
      getsockname(12, reinterpret_cast<sockaddr*>(&socket_address),
                  &socket_address_size) == 0 &&
      socket_address.ss_family == AF_UNIX;
  const auto report_ready = [readiness_socket_valid](char result) {
    if (!readiness_socket_valid) return false;
    const ssize_t sent = send(12, &result, sizeof(result), MSG_NOSIGNAL);
    close(12);
    return sent == 1;
  };
  std::array<char, 64> descriptor_target{};
  const ssize_t size = readlink("/proc/self/fd/10", descriptor_target.data(),
                                descriptor_target.size());
  struct stat metadata {};
  if (size <= 0 ||
      std::string_view(descriptor_target.data(), static_cast<std::size_t>(size)) !=
          "anon_inode:[pidfd]" ||
      fstat(11, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
      metadata.st_uid != geteuid() || flock(11, LOCK_EX | LOCK_NB) != 0 ||
      !readiness_socket_valid ||
      fcntl(10, F_SETFD, FD_CLOEXEC) != 0 ||
      fcntl(11, F_SETFD, FD_CLOEXEC) != 0) {
    std::cerr << "[native-updater] invalid runtime-exit handoff; update cancelled\n";
    (void)report_ready('E');
    return false;
  }
  // The launcher waits for this acknowledgement before it can close Aurora.
  // From here onward this process owns a valid pidfd and the inherited lock.
  if (!report_ready('R')) {
    // A normal Aurora close can race this startup handshake. The launcher
    // requires the ACK before an Update now close; if Aurora is already
    // exiting, the validated pidfd and lock still make deferred execution safe.
    std::cerr << "[native-updater] launcher closed the readiness channel; "
                 "continuing to wait for Aurora exit\n";
  }
  pollfd process{10, POLLIN, 0};
  int ready = -1;
  do {
    ready = poll(&process, 1, -1);
  } while (ready < 0 && errno == EINTR);
  if (ready != 1 || (process.revents & POLLIN) == 0 ||
      (process.revents & (POLLERR | POLLNVAL)) != 0) {
    std::cerr << "[native-updater] cannot confirm Aurora exit; update cancelled\n";
    return false;
  }
  close(10);
  // Keep FD 11 until main exits; CLOEXEC prevents canaries inheriting it.
  return true;
}

void Usage(std::ostream& output) {
  output << "Usage: aurora_updater COMMAND [OPTIONS]\n\n"
            "Commands:\n"
            "  update [--startup-preflight] [--no-latest-check] "
            "[--skip-canary]\n"
            "  refresh-current-audio-profile\n"
            "  check-latest\n"
            "  verify-current\n"
            "  verify-apk APK\n"
            "  prepare-apks OUTPUT APK [APK...]\n"
            "  derive-host-abi OUTPUT REF_LIB REF_PROFILE CANDIDATE_DIR "
            "[REF_COMPAT...]\n"
            "  status\n"
            "  rollback\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    Usage(std::cerr);
    return 2;
  }
  const aurora::update::UpdatePaths paths = ResolvePaths();
  const std::string_view command = argv[1];
  aurora::update::PayloadStore store(
      paths.data_root, paths.compatibility_manifest, paths.runtime_binary);
  if (command == "check-available") {
    if (argc != 2) return 2;
    const auto offer = aurora::update::CheckUpdateAvailability(paths);
    if (!offer.error.empty()) {
      std::cerr << "[native-updater] " << offer.error << '\n';
      return 1;
    }
    if (offer.available) return 10;
    return offer.newer_release_unavailable_for_guest_abi ? 11 : 0;
  }
  const bool after_exit = command == "update-after-exit";
  if (after_exit && (argc != 2 || !WaitForRuntimeExit())) return 1;
  if (command == "check-latest") {
    if (argc != 2) return 2;
    const aurora::update::ProviderVersion latest =
        aurora::update::CheckLatestRobloxVersion();
    if (!latest) {
      std::cerr << "[native-updater] " << latest.error << '\n';
      return 1;
    }
    std::cout << latest.version_name << ' ' << latest.version_code << '\n';
    return 0;
  }
  if (command == "status") {
    if (argc != 2) return 2;
    std::string error;
    const std::string status = store.StatusJson(&error);
    if (!error.empty()) {
      std::cerr << "[native-updater] " << error << '\n';
      return 1;
    }
    std::cout << status;
    return 0;
  }
  if (command == "verify-current") {
    if (argc != 2) return 2;
    const auto current = store.VerifyCurrent();
    if (!current) {
      std::cerr << "[native-updater] " << current.error << '\n';
      return 1;
    }
    std::cout << current.payload_id << '\n';
    return 0;
  }
  if (command == "verify-apk") {
    if (argc != 3) return 2;
    const std::filesystem::path apk = argv[2];
    const auto signature = aurora::update::VerifyApkSignature(apk);
    if (!signature) {
      std::cerr << "[native-updater] " << signature.error << '\n';
      return 1;
    }
    const auto encoded_manifest = aurora::update::ReadZipEntry(
        apk, "AndroidManifest.xml", 16U * 1024U * 1024U);
    if (!encoded_manifest) {
      std::cerr << "[native-updater] " << encoded_manifest.error << '\n';
      return 1;
    }
    const auto identity =
        aurora::update::ParseAndroidManifest(encoded_manifest.bytes);
    if (!identity) {
      std::cerr << "[native-updater] " << identity.error << '\n';
      return 1;
    }
    std::cout << identity.package_name << ' ' << identity.version_name << ' '
              << identity.version_code << ' '
              << (identity.split_name.empty() ? "base" : identity.split_name)
              << '\n';
    for (const std::string& certificate : signature.certificate_sha256) {
      std::cout << certificate << '\n';
    }
    return 0;
  }
  if (command == "prepare-apks") {
    if (argc < 4) return 2;
    std::vector<std::filesystem::path> archives;
    for (int index = 3; index < argc; ++index)
      archives.emplace_back(argv[index]);
    const auto encoded_manifest = aurora::update::ReadZipEntry(
        archives.front(), "AndroidManifest.xml", 16U * 1024U * 1024U);
    if (!encoded_manifest) {
      std::cerr << "[native-updater] " << encoded_manifest.error << '\n';
      return 1;
    }
    const auto identity =
        aurora::update::ParseAndroidManifest(encoded_manifest.bytes);
    const auto catalog = aurora::update::LoadCompatibilityCatalog(
        paths.compatibility_manifest);
    if (!identity || !catalog) {
      std::cerr << "[native-updater] "
                << (!identity ? identity.error : catalog.error) << '\n';
      return 1;
    }
    const auto profile = aurora::update::FindSupportedProfile(
        catalog.profiles, identity.version_name, identity.version_code);
    if (!profile.has_value()) {
      std::cerr << "[native-updater] APK is not exact-supported\n";
      return 1;
    }
    const auto prepared = aurora::update::PreparePayloadFromArchives(
        archives, aurora::update::ExactPayloadIdentity(*profile),
        paths.signing_trust_manifest, argv[2], "native-local-import");
    if (!prepared) {
      std::cerr << "[native-updater] " << prepared.error << '\n';
      return 1;
    }
    std::cout << prepared.directory << '\n';
    return 0;
  }
  if (command == "rollback") {
    if (argc != 2) return 2;
    const auto rolled_back = store.Rollback();
    if (!rolled_back) {
      std::cerr << "[native-updater] " << rolled_back.error << '\n';
      return 1;
    }
    std::cout << rolled_back.payload_id << '\n';
    return 0;
  }
  if (command == "refresh-current-audio-profile") {
    if (argc != 2) return 2;
    aurora::update::CanaryGraphicsBackend graphics_backend =
        aurora::update::CanaryGraphicsBackend::kDirectVulkan;
    const std::string configured_backend =
        Environment("AURORA_GRAPHICS_BACKEND").value_or("direct-vulkan");
    if (!aurora::update::ParseCanaryGraphicsBackend(configured_backend,
                                                    &graphics_backend)) {
      std::cerr << "[native-updater] unsupported graphics backend: "
                << configured_backend << '\n';
      return 2;
    }
    const auto refreshed = aurora::update::RefreshCurrentAudioOutputProfile(
        paths, graphics_backend, ProgressDescriptor());
    if (!refreshed) {
      std::cerr << "[native-updater] " << refreshed.error << '\n';
      return 1;
    }
    if (!refreshed.message.empty()) {
      std::cerr << "[native-updater] " << refreshed.message << '\n';
    }
    if (!refreshed.payload_id.empty()) std::cout << refreshed.payload_id << '\n';
    return 0;
  }
  if (command == "derive-host-abi") {
    if (argc < 6) return 2;
    aurora::update::HostAbiDerivationOptions options;
    options.output_directory = argv[2];
    options.reference_library = argv[3];
    options.reference_profile = argv[4];
    options.candidate_payload_directory = argv[5];
    for (int index = 6; index < argc; ++index) {
      options.reference_compatibility_manifests.emplace_back(argv[index]);
    }
    const auto derived = aurora::update::DeriveHostAbiProfile(options);
    if (!derived) {
      std::cerr << "[native-updater] " << derived.error << '\n';
      return 1;
    }
    std::cout << derived.profile << '\n'
              << derived.compatibility_manifest << '\n';
    return 0;
  }
  if (command != "update" && !after_exit) {
    Usage(std::cerr);
    return 2;
  }
  aurora::update::UpdateRequest request;
  request.progress_fd = ProgressDescriptor();
  const std::string graphics_backend =
      Environment("AURORA_GRAPHICS_BACKEND").value_or("direct-vulkan");
  if (!aurora::update::ParseCanaryGraphicsBackend(
          graphics_backend, &request.canary_graphics_backend)) {
    std::cerr << "[native-updater] unsupported graphics backend: "
              << graphics_backend << '\n';
    return 2;
  }
  for (int index = 2; index < argc; ++index) {
    const std::string_view option = argv[index];
    if (option == "--startup-preflight") {
      request.startup_preflight = true;
    } else if (option == "--no-latest-check") {
      request.check_latest = false;
    } else if (option == "--skip-canary") {
      request.run_canary = false;
    } else if (option == "--force-run-latest") {
      request.force_run_latest = true;
    } else {
      std::cerr << "[native-updater] unknown option: " << option << '\n';
      return 2;
    }
  }
  if (request.force_run_latest &&
      (request.startup_preflight || !request.check_latest || !request.run_canary)) {
    std::cerr << "[native-updater] --force-run-latest cannot be combined with "
                 "--startup-preflight, --no-latest-check, or --skip-canary\n";
    return 2;
  }
  if (request.force_run_latest) {
    std::cerr << "[native-updater] WARNING: latest Roblox will run once without "
                 "approval and will not become the active payload\n";
  }
  const aurora::update::UpdateResult updated =
      aurora::update::RunUpdate(paths, request);
  for (const std::string& warning : updated.warnings) {
    std::cerr << "[native-updater] warning: " << warning << '\n';
  }
  if (!updated) {
    std::cerr << "[native-updater] " << updated.error << '\n';
    return 1;
  }
  if (!updated.message.empty()) {
    std::cerr << "[native-updater] " << updated.message << '\n';
  }
  // Still exits zero so the session launches, but the user has to be told:
  // it is the Roblox servers, not Aurora, that will refuse the join.
  if (updated.stale) {
    std::cerr << "[native-updater] warning: the active Roblox payload is out "
                 "of date and joining an experience can be refused\n";
  }
  if (!updated.payload_id.empty()) std::cout << updated.payload_id << '\n';
  return 0;
}
