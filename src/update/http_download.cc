#include "update/http_download.h"

#include <curl/curl.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "update/payload_integrity.h"

namespace aurora::update {
namespace {

std::once_flag g_curl_once;
CURLcode g_curl_status = CURLE_FAILED_INIT;

void InitialiseCurl() { g_curl_status = curl_global_init(CURL_GLOBAL_DEFAULT); }

bool HostMatches(std::string_view host, std::string_view allowed) {
  if (host == allowed) return true;
  return host.size() > allowed.size() &&
         host[host.size() - allowed.size() - 1] == '.' &&
         host.substr(host.size() - allowed.size()) == allowed;
}

std::string Lower(std::string value) {
  for (char& character : value) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  return value;
}

bool StartsWithInsensitive(std::string_view value,
                           std::string_view prefix) {
  return value.size() >= prefix.size() &&
         Lower(std::string(value.substr(0, prefix.size()))) == prefix;
}

struct CurlUrlDeleter {
  void operator()(CURLU* url) const {
    if (url != nullptr) curl_url_cleanup(url);
  }
};

struct CurlStringDeleter {
  void operator()(char* value) const {
    if (value != nullptr) curl_free(value);
  }
};

struct CurlHandleDeleter {
  void operator()(CURL* handle) const {
    if (handle != nullptr) curl_easy_cleanup(handle);
  }
};

struct CurlHeadersDeleter {
  void operator()(curl_slist* headers) const {
    if (headers != nullptr) curl_slist_free_all(headers);
  }
};

bool RedirectStatus(long status) {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

bool RetryableTransport(CURLcode status) {
  switch (status) {
    case CURLE_COULDNT_CONNECT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_PARTIAL_FILE:
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_SSL_CONNECT_ERROR:
      return true;
    default:
      return false;
  }
}

bool RetryableStatus(long status) {
  return status == 408 || status == 425 || status == 429 || status >= 500;
}

void Sleep(long milliseconds) {
  if (milliseconds <= 0) return;
  struct timespec delay = {milliseconds / 1000,
                           (milliseconds % 1000) * 1000000L};
  while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
  }
}

void PublishMessage(int descriptor, const std::string& message) {
  if (descriptor < 0) return;
  const std::string packet = "P" + message;
  (void)send(descriptor, packet.data(), packet.size(), MSG_NOSIGNAL);
}

constexpr curl_off_t kMebibyte = 1024 * 1024;

struct TransferProgress {
  int descriptor = -1;
  curl_off_t resumed_bytes = 0;
  std::chrono::steady_clock::time_point reported_at{};
  curl_off_t reported_bytes = 0;
  bool started = false;
};

using TransferDeadline =
    std::optional<std::chrono::steady_clock::time_point>;

TransferDeadline MakeDeadline(const HttpTransferRequest& request) {
  if (request.total_timeout_ms <= 0) return std::nullopt;
  return std::chrono::steady_clock::now() +
         std::chrono::milliseconds(request.total_timeout_ms);
}

bool RemainingMilliseconds(const TransferDeadline& deadline, long* remaining) {
  if (!deadline.has_value()) {
    if (remaining != nullptr) *remaining = 0;
    return true;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now >= *deadline) {
    if (remaining != nullptr) *remaining = 0;
    return false;
  }
  const auto value =
      std::chrono::ceil<std::chrono::milliseconds>(*deadline - now).count();
  if (remaining != nullptr) {
    *remaining = value > std::numeric_limits<long>::max()
                     ? std::numeric_limits<long>::max()
                     : static_cast<long>(value);
  }
  return true;
}

long EffectiveTimeout(long configured, long remaining) {
  return configured <= 0 ? remaining : std::min(configured, remaining);
}

bool SleepBeforeRetry(long delay, int attempt,
                      const TransferDeadline& deadline) {
  if (delay <= 0 || attempt <= 0) return RemainingMilliseconds(deadline, nullptr);
  const long long maximum = std::numeric_limits<long long>::max();
  const long long scaled =
      delay > maximum / attempt ? maximum
                                : static_cast<long long>(delay) * attempt;
  long bounded = scaled > std::numeric_limits<long>::max()
                     ? std::numeric_limits<long>::max()
                     : static_cast<long>(scaled);
  long remaining = 0;
  if (!RemainingMilliseconds(deadline, &remaining)) return false;
  if (deadline.has_value()) bounded = std::min(bounded, remaining);
  Sleep(bounded);
  return RemainingMilliseconds(deadline, nullptr);
}

// One decimal place without dragging in a stream: 1536 KiB/s reads as 1.5.
std::string Rate(curl_off_t bytes, std::chrono::milliseconds elapsed) {
  if (elapsed.count() <= 0) return {};
  const curl_off_t per_second = bytes * 1000 / elapsed.count();
  if (per_second < kMebibyte) {
    return " at " + std::to_string(per_second / 1024) + " KiB/s";
  }
  const curl_off_t tenths = per_second * 10 / kMebibyte;
  return " at " + std::to_string(tenths / 10) + "." +
         std::to_string(tenths % 10) + " MiB/s";
}

// Refreshed once a second: a dialog that only moves every 64 MiB looks frozen.
int ReportTransferProgress(void* opaque, curl_off_t expected, curl_off_t now,
                           curl_off_t, curl_off_t) {
  auto* progress = static_cast<TransferProgress*>(opaque);
  if (progress == nullptr || progress->descriptor < 0) return 0;
  const auto moment = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      moment - progress->reported_at);
  if (progress->started && elapsed < std::chrono::seconds(1)) return 0;
  const curl_off_t received = progress->resumed_bytes + now;
  const curl_off_t total =
      expected > 0 ? progress->resumed_bytes + expected : 0;
  std::string message =
      "Downloading Roblox... " + std::to_string(received / kMebibyte) + " MiB";
  if (total > 0) {
    message += " / " + std::to_string(total / kMebibyte) + " MiB (" +
               std::to_string(received * 100 / total) + "%)";
  }
  if (progress->started && received > progress->reported_bytes) {
    message += Rate(received - progress->reported_bytes, elapsed);
  }
  progress->started = true;
  progress->reported_at = moment;
  progress->reported_bytes = received;
  PublishMessage(progress->descriptor, message);
  return 0;
}

struct BytesWriter {
  std::string* output = nullptr;
  std::size_t maximum = 0;
  bool exceeded = false;
};

std::size_t WriteBytes(char* data, std::size_t size, std::size_t count,
                       void* opaque) {
  auto* writer = static_cast<BytesWriter*>(opaque);
  if (writer == nullptr || writer->output == nullptr || data == nullptr ||
      (count != 0 && size > std::numeric_limits<std::size_t>::max() / count)) {
    return 0;
  }
  const std::size_t bytes = size * count;
  if (bytes > writer->maximum ||
      writer->output->size() > writer->maximum - bytes) {
    writer->exceeded = true;
    return 0;
  }
  writer->output->append(data, bytes);
  return bytes;
}

struct FileWriter {
  int descriptor = -1;
  std::size_t maximum = 0;
  // Includes any resumed prefix, so the limit covers the whole file.
  std::size_t written = 0;
  // Null while resuming; the digest is then taken in one pass at the end.
  FileDigest* sha256 = nullptr;
  bool exceeded = false;
  bool write_failed = false;
  bool hash_failed = false;
};

std::size_t WriteFileBytes(char* data, std::size_t size, std::size_t count,
                           void* opaque) {
  auto* writer = static_cast<FileWriter*>(opaque);
  if (writer == nullptr || writer->descriptor < 0 || data == nullptr ||
      (count != 0 && size > std::numeric_limits<std::size_t>::max() / count)) {
    return 0;
  }
  const std::size_t bytes = size * count;
  if (bytes > writer->maximum || writer->written > writer->maximum - bytes) {
    writer->exceeded = true;
    return 0;
  }
  std::size_t offset = 0;
  while (offset < bytes) {
    const ssize_t result =
        write(writer->descriptor, data + offset, bytes - offset);
    if (result < 0) {
      if (errno == EINTR) continue;
      writer->write_failed = true;
      return 0;
    }
    offset += static_cast<std::size_t>(result);
  }
  if (writer->sha256 != nullptr) {
    if (!writer->sha256->Update(data, bytes)) {
      writer->hash_failed = true;
      return 0;
    }
  }
  writer->written += bytes;
  return bytes;
}

bool Configure(CURL* handle, const HttpTransferRequest& request,
               std::string_view url, curl_slist* headers,
               std::array<char, CURL_ERROR_SIZE>* error_buffer,
               const TransferDeadline& deadline, std::string* error) {
  long remaining = 0;
  if (!RemainingMilliseconds(deadline, &remaining)) {
    if (error != nullptr) *error = "HTTPS transfer deadline exceeded";
    return false;
  }
  const long connect_timeout = deadline.has_value()
                                   ? EffectiveTimeout(request.connect_timeout_ms,
                                                      remaining)
                                   : request.connect_timeout_ms;
  const long transfer_timeout = deadline.has_value()
                                    ? EffectiveTimeout(request.transfer_timeout_ms,
                                                       remaining)
                                    : request.transfer_timeout_ms;
  return curl_easy_setopt(handle, CURLOPT_URL, std::string(url).c_str()) ==
             CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L) == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https") ==
             CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                          connect_timeout) == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                          transfer_timeout) == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT,
                          request.low_speed_bytes_per_second) == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME,
                          request.low_speed_seconds) == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "") == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_USERAGENT,
                          "Aurora-native-updater/1") == CURLE_OK &&
         curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer->data()) ==
             CURLE_OK &&
         (headers == nullptr ||
          curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers) == CURLE_OK);
}

std::unique_ptr<curl_slist, CurlHeadersDeleter> BuildHeaders(
    const std::vector<std::string>& values, std::string* error) {
  curl_slist* list = nullptr;
  for (const std::string& value : values) {
    curl_slist* next = curl_slist_append(list, value.c_str());
    if (next == nullptr) {
      curl_slist_free_all(list);
      *error = "cannot allocate HTTP request headers";
      return {};
    }
    list = next;
  }
  return std::unique_ptr<curl_slist, CurlHeadersDeleter>(list);
}

std::string RedirectUrl(CURL* handle) {
  char* value = nullptr;
  if (curl_easy_getinfo(handle, CURLINFO_REDIRECT_URL, &value) != CURLE_OK ||
      value == nullptr) {
    return {};
  }
  return value;
}

struct RedirectHeader {
  std::string location;
  std::size_t location_count = 0;
  bool malformed = false;
};

std::size_t CaptureRedirectHeader(char* data, std::size_t size,
                                  std::size_t count, void* opaque) {
  if (opaque == nullptr || (count != 0 &&
                            size > std::numeric_limits<std::size_t>::max() /
                                       count)) {
    return 0;
  }
  const std::size_t bytes = size * count;
  auto* header = static_cast<RedirectHeader*>(opaque);
  const std::string_view line(data, bytes);
  if (StartsWithInsensitive(line, "HTTP/")) {
    header->location.clear();
    header->location_count = 0;
    header->malformed = false;
    return bytes;
  }
  const std::size_t colon = line.find(':');
  if (colon == std::string_view::npos ||
      Lower(std::string(line.substr(0, colon))) != "location") {
    return bytes;
  }
  ++header->location_count;
  if (header->location_count != 1) {
    header->malformed = true;
    return bytes;
  }
  std::size_t begin = colon + 1;
  while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) {
    ++begin;
  }
  std::size_t end = line.size();
  while (end > begin &&
         (line[end - 1] == '\r' || line[end - 1] == '\n' ||
          line[end - 1] == ' ' || line[end - 1] == '\t')) {
    --end;
  }
  header->location.assign(line.substr(begin, end - begin));
  if (header->location.empty()) header->malformed = true;
  return bytes;
}

std::string CurlFailure(CURLcode status,
                        const std::array<char, CURL_ERROR_SIZE>& buffer) {
  return buffer[0] != '\0' ? std::string(buffer.data())
                           : std::string(curl_easy_strerror(status));
}

std::string UrlHost(std::string_view url) {
  std::unique_ptr<CURLU, CurlUrlDeleter> parsed(curl_url());
  char* raw_host = nullptr;
  if (!parsed ||
      curl_url_set(parsed.get(), CURLUPART_URL, std::string(url).c_str(), 0) !=
          CURLUE_OK ||
      curl_url_get(parsed.get(), CURLUPART_HOST, &raw_host, 0) != CURLUE_OK ||
      raw_host == nullptr) {
    return {};
  }
  std::unique_ptr<char, CurlStringDeleter> host(raw_host);
  return Lower(host.get());
}

bool HasRawUserinfo(std::string_view url) {
  const std::size_t scheme = url.find("://");
  if (scheme == std::string_view::npos) return false;
  const std::size_t begin = scheme + 3;
  const std::size_t end = url.find_first_of("/?#", begin);
  const std::string_view authority =
      url.substr(begin, end == std::string_view::npos ? url.size() - begin
                                                       : end - begin);
  return authority.find('@') != std::string_view::npos;
}

std::optional<std::string_view> RawPathFromUrl(std::string_view url) {
  const std::size_t scheme = url.find("://");
  if (scheme == std::string_view::npos) return std::nullopt;
  const std::size_t authority_begin = scheme + 3;
  const std::size_t authority_end = url.find_first_of("/?#", authority_begin);
  if (authority_end == std::string_view::npos ||
      url[authority_end] == '?' || url[authority_end] == '#') {
    return std::string_view("/");
  }
  const std::size_t path_end = url.find_first_of("?#", authority_end);
  return url.substr(authority_end,
                    path_end == std::string_view::npos
                        ? url.size() - authority_end
                        : path_end - authority_end);
}

bool SafeRawPath(std::string_view path) {
  if (path.empty() || path.front() != '/' ||
      path.find('%') != std::string_view::npos ||
      path.find('\\') != std::string_view::npos ||
      path.find(';') != std::string_view::npos ||
      path.find("//") != std::string_view::npos) {
    return false;
  }
  std::size_t segment_begin = 1;
  for (std::size_t index = 1; index <= path.size(); ++index) {
    if (index < path.size()) {
      const unsigned char character = static_cast<unsigned char>(path[index]);
      if (character <= 0x20 || character >= 0x7f || character == '?' ||
          character == '#') {
        return false;
      }
      const bool unreserved =
          std::isalnum(character) || character == '-' || character == '.' ||
          character == '_' || character == '~';
      const bool sub_delim = character == '!' || character == '$' ||
                             character == '&' || character == '\'' ||
                             character == '(' || character == ')' ||
                             character == '*' || character == '+' ||
                             character == ',' || character == '=';
      if (character != '/' && character != ':' && character != '@' &&
          !unreserved && !sub_delim) {
        return false;
      }
    }
    if (index == path.size() || path[index] == '/') {
      const std::string_view segment = path.substr(segment_begin,
                                                   index - segment_begin);
      if (segment == "." || segment == "..") return false;
      segment_begin = index + 1;
    }
  }
  return true;
}

bool ForbiddenRawPath(std::string_view path,
                      const std::vector<std::string>& forbidden_paths) {
  if (!SafeRawPath(path)) return true;
  const std::string normalized_path = Lower(std::string(path));
  for (const std::string& forbidden : forbidden_paths) {
    const std::string normalized_forbidden = Lower(forbidden);
    if (normalized_forbidden.empty()) continue;
    if (normalized_path == normalized_forbidden ||
        (normalized_path.size() > normalized_forbidden.size() &&
         normalized_path.compare(0, normalized_forbidden.size(),
                                 normalized_forbidden) == 0 &&
         (normalized_forbidden.back() == '/' ||
          normalized_path[normalized_forbidden.size()] == '/'))) {
      return true;
    }
  }
  return false;
}

bool ForbiddenPath(std::string_view url,
                   const std::vector<std::string>& forbidden_paths) {
  if (forbidden_paths.empty()) return false;
  const auto path = RawPathFromUrl(url);
  return !path.has_value() || ForbiddenRawPath(*path, forbidden_paths);
}

bool RedirectPathAllowed(std::string_view location,
                         const std::vector<std::string>& forbidden_paths) {
  if (forbidden_paths.empty()) return true;
  std::optional<std::string_view> path;
  if (StartsWithInsensitive(location, "https://")) {
    path = RawPathFromUrl(location);
  } else if (!location.empty() && location.front() == '/' &&
             (location.size() == 1 || location[1] != '/')) {
    const std::size_t end = location.find_first_of("?#");
    path = location.substr(0, end == std::string_view::npos
                                  ? location.size()
                                  : end);
  }
  return path.has_value() && !ForbiddenRawPath(*path, forbidden_paths);
}

bool TrustedTransferUrl(std::string_view url,
                        const HttpTransferRequest& request,
                        std::string* error) {
  if (!IsTrustedHttpsUrl(url, request.allowed_hosts, error,
                         request.exact_host_matches)) {
    return false;
  }
  if (ForbiddenPath(url, request.forbidden_paths)) {
    if (error != nullptr) *error = "HTTPS URL path is prohibited";
    return false;
  }
  return true;
}

// Provider outages are the most common first-run failure, so the host that
// rejected the transfer belongs in the message the user actually reads.
std::string StatusFailure(std::string_view action, std::string_view url,
                          long status_code) {
  const std::string host = UrlHost(url);
  return (host.empty() ? std::string("HTTPS ") + std::string(action)
                       : host + " " + std::string(action)) +
         " returned status " + std::to_string(status_code);
}

HttpBytesResult DownloadBytesAttempt(const HttpTransferRequest& request,
                                     const TransferDeadline& deadline,
                                     bool* retryable) {
  HttpBytesResult result;
  std::string current = request.url;
  for (int redirect = 0; redirect <= request.maximum_redirects; ++redirect) {
    std::unique_ptr<CURL, CurlHandleDeleter> handle(curl_easy_init());
    if (!handle) {
      result.error = "curl_easy_init failed";
      return result;
    }
    auto headers = BuildHeaders(request.headers, &result.error);
    if (!request.headers.empty() && !headers) return result;
    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    if (!Configure(handle.get(), request, current, headers.get(),
                   &error_buffer, deadline, &result.error)) {
      if (result.error.empty()) result.error = "cannot configure HTTPS request";
      return result;
    }
    RedirectHeader redirect_header;
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION,
                     CaptureRedirectHeader);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &redirect_header);
    result.bytes.clear();
    BytesWriter writer{&result.bytes, request.maximum_bytes, false};
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, WriteBytes);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &writer);
    const CURLcode status = curl_easy_perform(handle.get());
    if (!RemainingMilliseconds(deadline, nullptr)) {
      result.error = "HTTPS transfer deadline exceeded";
      return result;
    }
    if (status != CURLE_OK) {
      result.error = writer.exceeded ? "HTTPS response exceeds its size limit"
                                     : CurlFailure(status, error_buffer);
      *retryable = !writer.exceeded && RetryableTransport(status);
      return result;
    }
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE,
                      &result.status_code);
    if (RedirectStatus(result.status_code)) {
      if (request.forbidden_paths.size() > 0 &&
          (redirect_header.malformed ||
           redirect_header.location_count != 1 ||
           !RedirectPathAllowed(redirect_header.location,
                               request.forbidden_paths))) {
        result.error = "HTTPS redirect path is ambiguous or prohibited";
        return result;
      }
      const std::string next = RedirectUrl(handle.get());
      if (next.empty() || redirect == request.maximum_redirects ||
          !TrustedTransferUrl(next, request, &result.error)) {
        if (result.error.empty()) result.error = "invalid HTTPS redirect";
        return result;
      }
      current = next;
      continue;
    }
    if (result.status_code < 200 || result.status_code >= 300) {
      result.error = StatusFailure("request", current, result.status_code);
      *retryable = RetryableStatus(result.status_code);
      return result;
    }
    result.final_url = current;
    return result;
  }
  result.error = "too many HTTPS redirects";
  return result;
}

// Leaves the transferred bytes in `temporary`: they survive a retryable
// failure so the next attempt resumes instead of pulling the archive again.
HttpDownloadResult DownloadFileAttempt(const HttpTransferRequest& request,
                                       const std::filesystem::path& temporary,
                                       int progress_fd,
                                       const TransferDeadline& deadline,
                                       bool* retryable) {
  HttpDownloadResult result;
  std::error_code filesystem_error;
  std::uintmax_t existing = std::filesystem::file_size(temporary,
                                                       filesystem_error);
  if (filesystem_error || existing > request.maximum_bytes) {
    std::filesystem::remove(temporary, filesystem_error);
    existing = 0;
  }
  // Outlives the redirect hops, or the count restarts at zero on each one.
  TransferProgress progress;
  progress.descriptor = progress_fd;
  progress.resumed_bytes = static_cast<curl_off_t>(existing);
  std::string current = request.url;
  for (int redirect = 0; redirect <= request.maximum_redirects; ++redirect) {
    const bool resuming = existing > 0;
    const int descriptor =
        open(temporary.c_str(),
             O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW |
                 (resuming ? 0 : O_TRUNC),
             0600);
    if (descriptor < 0) {
      result.error = "cannot create temporary download: " +
                     std::string(std::strerror(errno));
      return result;
    }
    if (resuming && lseek(descriptor, 0, SEEK_END) < 0) {
      close(descriptor);
      std::filesystem::remove(temporary, filesystem_error);
      result.error = "cannot resume temporary download: " +
                     std::string(std::strerror(errno));
      return result;
    }
    std::unique_ptr<CURL, CurlHandleDeleter> handle(curl_easy_init());
    auto headers = BuildHeaders(request.headers, &result.error);
    if (!handle || (!request.headers.empty() && !headers)) {
      close(descriptor);
      std::filesystem::remove(temporary, filesystem_error);
      if (result.error.empty()) result.error = "curl_easy_init failed";
      return result;
    }
    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    if (!Configure(handle.get(), request, current, headers.get(),
                   &error_buffer, deadline, &result.error)) {
      close(descriptor);
      std::filesystem::remove(temporary, filesystem_error);
      if (result.error.empty()) result.error = "cannot configure HTTPS download";
      return result;
    }
    RedirectHeader redirect_header;
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION,
                     CaptureRedirectHeader);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &redirect_header);
    FileDigest sha256;
    if (!resuming && !sha256.valid()) {
      close(descriptor);
      std::filesystem::remove(temporary, filesystem_error);
      result.error = "cannot initialize sha256 digest";
      return result;
    }
    FileWriter writer{descriptor, request.maximum_bytes,
                      static_cast<std::size_t>(existing),
                      resuming ? nullptr : &sha256, false, false};
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, WriteFileBytes);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &writer);
    curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS,
                     progress_fd < 0 ? 1L : 0L);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION,
                     ReportTransferProgress);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &progress);
    if (resuming) {
      curl_easy_setopt(handle.get(), CURLOPT_RESUME_FROM_LARGE,
                       static_cast<curl_off_t>(existing));
    }
    const CURLcode status = curl_easy_perform(handle.get());
    long http_status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &http_status);
    const bool synced = fsync(descriptor) == 0;
    close(descriptor);
    if (!RemainingMilliseconds(deadline, nullptr)) {
      std::filesystem::remove(temporary, filesystem_error);
      result.error = "HTTPS transfer deadline exceeded";
      return result;
    }
    if (status != CURLE_OK || writer.write_failed || writer.hash_failed ||
        !synced) {
      if (writer.hash_failed) {
        result.error = "cannot compute SHA-256 digest during download";
      } else if (writer.exceeded) {
        result.error = "HTTPS download exceeds its size limit";
      } else if (writer.write_failed || !synced) {
        result.error = "cannot persist HTTPS download";
      } else {
        result.error = CurlFailure(status, error_buffer);
        *retryable = RetryableTransport(status);
      }
      const bool failed_with_non_2xx_response =
          status != CURLE_OK && http_status != 0 &&
          (http_status < 200 || http_status >= 300);
      if (failed_with_non_2xx_response) {
        filesystem_error.clear();
        std::filesystem::remove(temporary, filesystem_error);
        if (filesystem_error) {
          *retryable = false;
          result.error = "cannot discard HTTP error response before retry";
        }
      } else if (!*retryable) {
        std::filesystem::remove(temporary, filesystem_error);
      }
      return result;
    }
    if (RedirectStatus(http_status)) {
      // The redirect body was appended to the file; drop it, keep the prefix.
      if (truncate(temporary.c_str(), static_cast<off_t>(existing)) != 0) {
        std::filesystem::remove(temporary, filesystem_error);
        existing = 0;
      }
      if (request.forbidden_paths.size() > 0 &&
          (redirect_header.malformed ||
           redirect_header.location_count != 1 ||
           !RedirectPathAllowed(redirect_header.location,
                               request.forbidden_paths))) {
        std::filesystem::remove(temporary, filesystem_error);
        result.error = "HTTPS redirect path is ambiguous or prohibited";
        return result;
      }
      const std::string next = RedirectUrl(handle.get());
      if (next.empty() || redirect == request.maximum_redirects ||
          !TrustedTransferUrl(next, request, &result.error)) {
        std::filesystem::remove(temporary, filesystem_error);
        if (result.error.empty()) result.error = "invalid HTTPS redirect";
        return result;
      }
      current = next;
      continue;
    }
    // The range was ignored or the offset refused; either way the partial
    // file is unusable now.
    if (resuming && (http_status == 200 || http_status == 416)) {
      std::filesystem::remove(temporary, filesystem_error);
      result.error = http_status == 416
                         ? "HTTPS server rejected the resume offset"
                         : "HTTPS server ignored the resume request";
      *retryable = true;
      return result;
    }
    if (http_status < 200 || http_status >= 300 || writer.written == 0) {
      result.error = writer.written == 0
                         ? "HTTPS download is empty"
                         : StatusFailure("download", current, http_status);
      *retryable = RetryableStatus(http_status);
      if (*retryable && (http_status < 200 || http_status >= 300)) {
        // A retryable HTTP error body is not a prefix of the requested
        // archive. Start the next attempt at byte zero; only retain partial
        // bytes after retryable transport failures above.
        filesystem_error.clear();
        std::filesystem::remove(temporary, filesystem_error);
        if (filesystem_error) {
          *retryable = false;
          result.error = "cannot discard HTTP error response before retry";
        }
      } else if (!*retryable) {
        std::filesystem::remove(temporary, filesystem_error);
      }
      return result;
    }
    result.bytes_written = writer.written;
    result.sha256 = resuming ? HashRegularFile(temporary, &result.error)
                             : sha256.FinalHex();
    if (result.sha256.empty()) {
      std::filesystem::remove(temporary, filesystem_error);
      if (result.error.empty()) {
        result.error = "cannot finalize SHA-256 digest of downloaded file";
      }
      return result;
    }
    result.final_url = current;
    return result;
  }
  std::filesystem::remove(temporary, filesystem_error);
  result.error = "too many HTTPS redirects";
  return result;
}

bool PathMatchesOpenFile(const std::filesystem::path& path, int descriptor) {
  struct stat opened_status {};
  struct stat path_status {};
  return descriptor >= 0 && fstat(descriptor, &opened_status) == 0 &&
         lstat(path.c_str(), &path_status) == 0 &&
         S_ISREG(opened_status.st_mode) && S_ISREG(path_status.st_mode) &&
         opened_status.st_dev == path_status.st_dev &&
         opened_status.st_ino == path_status.st_ino;
}

bool RemovePathIfOwned(const std::filesystem::path& path, int descriptor) {
  return PathMatchesOpenFile(path, descriptor) && unlink(path.c_str()) == 0;
}

}  // namespace

bool IsTrustedHttpsUrl(std::string_view url,
                       const std::vector<std::string>& allowed_hosts,
                       std::string* error, bool exact_host_matches) {
  std::unique_ptr<CURLU, CurlUrlDeleter> parsed(curl_url());
  if (!parsed || curl_url_set(parsed.get(), CURLUPART_URL,
                              std::string(url).c_str(), 0) != CURLUE_OK) {
    if (error != nullptr) *error = "URL is malformed";
    return false;
  }
  char* raw_scheme = nullptr;
  char* raw_host = nullptr;
  char* raw_port = nullptr;
  char* raw_user = nullptr;
  char* raw_password = nullptr;
  char* raw_options = nullptr;
  const CURLUcode scheme_status =
      curl_url_get(parsed.get(), CURLUPART_SCHEME, &raw_scheme, 0);
  const CURLUcode host_status =
      curl_url_get(parsed.get(), CURLUPART_HOST, &raw_host, 0);
  const CURLUcode port_status =
      curl_url_get(parsed.get(), CURLUPART_PORT, &raw_port, 0);
  const CURLUcode user_status =
      curl_url_get(parsed.get(), CURLUPART_USER, &raw_user, 0);
  const CURLUcode password_status =
      curl_url_get(parsed.get(), CURLUPART_PASSWORD, &raw_password, 0);
  const CURLUcode options_status =
      curl_url_get(parsed.get(), CURLUPART_OPTIONS, &raw_options, 0);
  std::unique_ptr<char, CurlStringDeleter> scheme(raw_scheme);
  std::unique_ptr<char, CurlStringDeleter> host(raw_host);
  std::unique_ptr<char, CurlStringDeleter> port(raw_port);
  std::unique_ptr<char, CurlStringDeleter> user(raw_user);
  std::unique_ptr<char, CurlStringDeleter> password(raw_password);
  std::unique_ptr<char, CurlStringDeleter> options(raw_options);
  if (scheme_status != CURLUE_OK || host_status != CURLUE_OK ||
      scheme == nullptr || host == nullptr || Lower(scheme.get()) != "https" ||
      (port_status == CURLUE_OK && port != nullptr &&
       std::string_view(port.get()) != "443") ||
      user_status != CURLUE_NO_USER ||
      password_status != CURLUE_NO_PASSWORD ||
      options_status != CURLUE_NO_OPTIONS || HasRawUserinfo(url)) {
    if (error != nullptr)
      *error = "URL must be credential-free HTTPS on port 443";
    return false;
  }
  std::string normalized_host = Lower(host.get());
  if (!exact_host_matches) {
    while (!normalized_host.empty() && normalized_host.back() == '.') {
      normalized_host.pop_back();
    }
  }
  for (const std::string& allowed : allowed_hosts) {
    const std::string normalized_allowed = Lower(allowed);
    if ((exact_host_matches && normalized_host == normalized_allowed) ||
        (!exact_host_matches &&
         HostMatches(normalized_host, normalized_allowed))) {
      return true;
    }
  }
  if (error != nullptr) *error = "URL host is not trusted";
  return false;
}

HttpBytesResult DownloadBytes(const HttpTransferRequest& request) {
  HttpBytesResult result;
  const TransferDeadline deadline = MakeDeadline(request);
  if (!RemainingMilliseconds(deadline, nullptr)) {
    result.error = "HTTPS transfer deadline exceeded";
    return result;
  }
  if (request.maximum_bytes == 0 ||
      !TrustedTransferUrl(request.url, request, &result.error)) {
    if (result.error.empty()) result.error = "HTTP byte limit is zero";
    return result;
  }
  std::call_once(g_curl_once, InitialiseCurl);
  if (g_curl_status != CURLE_OK) {
    result.error = curl_easy_strerror(g_curl_status);
    return result;
  }
  const int attempts =
      request.maximum_attempts > 0 ? request.maximum_attempts : 1;
  for (int attempt = 1;; ++attempt) {
    bool retryable = false;
    result = DownloadBytesAttempt(request, deadline, &retryable);
    if (result || !retryable || attempt >= attempts) return result;
    if (!SleepBeforeRetry(request.retry_delay_ms, attempt, deadline)) {
      result.bytes.clear();
      result.final_url.clear();
      result.error = "HTTPS transfer deadline exceeded";
      return result;
    }
  }
}

HttpDownloadResult DownloadFile(const HttpTransferRequest& request,
                                const std::filesystem::path& destination,
                                int progress_fd) {
  HttpDownloadResult result;
  const TransferDeadline deadline = MakeDeadline(request);
  if (!RemainingMilliseconds(deadline, nullptr)) {
    result.error = "HTTPS transfer deadline exceeded";
    return result;
  }
  if (request.maximum_bytes == 0 ||
      !TrustedTransferUrl(request.url, request, &result.error)) {
    if (result.error.empty()) result.error = "HTTP byte limit is zero";
    return result;
  }
  std::call_once(g_curl_once, InitialiseCurl);
  if (g_curl_status != CURLE_OK) {
    result.error = curl_easy_strerror(g_curl_status);
    return result;
  }
  std::error_code filesystem_error;
  std::filesystem::create_directories(destination.parent_path(),
                                      filesystem_error);
  if (filesystem_error) {
    result.error = "cannot create download directory";
    return result;
  }
  const bool deadline_bound = deadline.has_value();
  if (deadline_bound) {
    struct stat existing_status {};
    if (lstat(destination.c_str(), &existing_status) == 0) {
      result.error = "deadline-bound download requires an unused destination";
      return result;
    }
    if (errno != ENOENT) {
      result.error = "cannot verify download destination";
      return result;
    }
  }
  const std::filesystem::path temporary =
      destination.parent_path() / ("." + destination.filename().string() +
                                   ".part-" + std::to_string(getpid()));
  std::filesystem::remove(temporary, filesystem_error);
  const int attempts =
      request.maximum_attempts > 0 ? request.maximum_attempts : 1;
  for (int attempt = 1;; ++attempt) {
    bool retryable = false;
    result = DownloadFileAttempt(request, temporary, progress_fd, deadline,
                                 &retryable);
    if (result) break;
    if (!retryable || attempt >= attempts) {
      std::filesystem::remove(temporary, filesystem_error);
      return result;
    }
    PublishMessage(progress_fd, "Retrying Roblox download...");
    if (!SleepBeforeRetry(request.retry_delay_ms, attempt, deadline)) {
      std::filesystem::remove(temporary, filesystem_error);
      result.final_url.clear();
      result.error = "HTTPS transfer deadline exceeded";
      return result;
    }
  }
  if (!RemainingMilliseconds(deadline, nullptr)) {
    std::filesystem::remove(temporary, filesystem_error);
    result.final_url.clear();
    result.error = "HTTPS transfer deadline exceeded";
    return result;
  }
  if (deadline_bound) {
    const int owned_descriptor =
        open(temporary.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat owned_status {};
    if (owned_descriptor < 0 || fstat(owned_descriptor, &owned_status) != 0 ||
        !S_ISREG(owned_status.st_mode)) {
      if (owned_descriptor >= 0) close(owned_descriptor);
      std::filesystem::remove(temporary, filesystem_error);
      result.final_url.clear();
      result.error = "cannot verify completed download ownership";
      return result;
    }
    if (!RemainingMilliseconds(deadline, nullptr)) {
      close(owned_descriptor);
      std::filesystem::remove(temporary, filesystem_error);
      result.final_url.clear();
      result.error = "HTTPS transfer deadline exceeded";
      return result;
    }
    if (link(temporary.c_str(), destination.c_str()) != 0) {
      close(owned_descriptor);
      std::filesystem::remove(temporary, filesystem_error);
      result.final_url.clear();
      result.error = "cannot publish completed download";
      return result;
    }
    if (!PathMatchesOpenFile(destination, owned_descriptor)) {
      close(owned_descriptor);
      std::filesystem::remove(temporary, filesystem_error);
      result.final_url.clear();
      result.error = "cannot verify published download ownership";
      return result;
    }
    if (!RemainingMilliseconds(deadline, nullptr)) {
      (void)RemovePathIfOwned(destination, owned_descriptor);
      (void)unlink(temporary.c_str());
      close(owned_descriptor);
      result.final_url.clear();
      result.error = "HTTPS transfer deadline exceeded";
      return result;
    }
    if (unlink(temporary.c_str()) != 0) {
      (void)RemovePathIfOwned(destination, owned_descriptor);
      close(owned_descriptor);
      result.final_url.clear();
      result.error = "cannot finalize completed download";
      return result;
    }
    if (!RemainingMilliseconds(deadline, nullptr)) {
      (void)RemovePathIfOwned(destination, owned_descriptor);
      close(owned_descriptor);
      result.final_url.clear();
      result.error = "HTTPS transfer deadline exceeded";
      return result;
    }
    close(owned_descriptor);
    return result;
  }
  std::filesystem::rename(temporary, destination, filesystem_error);
  if (filesystem_error) {
    std::filesystem::remove(temporary, filesystem_error);
    result.error = "cannot publish completed download";
  }
  return result;
}

}  // namespace aurora::update
