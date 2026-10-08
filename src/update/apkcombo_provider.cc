// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include "update/apkcombo_provider.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "update/http_download.h"

namespace aurora::update {
namespace {

constexpr std::size_t kMaximumMetadataBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumArchiveBytes = 512U * 1024U * 1024U;
constexpr std::string_view kProductUrl =
    "https://apkcombo.com/roblox/com.roblox.client/";
constexpr std::string_view kProductPath = "/roblox/com.roblox.client/";
constexpr std::string_view kVersionPathPrefix =
    "/roblox/com.roblox.client/download/phone-";
constexpr std::string_view kVersionPathSuffix = "-apk";
constexpr std::string_view kApkComboHost = "apkcombo.com";
constexpr std::string_view kArtifactHost =
    "apks.39b7cb94d40914bac590886981b0ed6e.r2.cloudflarestorage.com";
constexpr std::string_view kVersionedPagePrefix =
    "https://apkcombo.com/roblox/com.roblox.client/download/phone-";
constexpr std::string_view kVersionedPageSuffix = "-apk";

struct HtmlLink {
  std::string href;
  std::string text;
};

struct HtmlDocument {
  std::string visible_text;
  std::vector<HtmlLink> links;
  std::vector<std::pair<std::string, std::string>> version_descriptions;
};

struct ParsedRelease {
  ProviderVersion version;
  std::string artifact_url;
};

char LowerAscii(char character) {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character - 'A' + 'a');
  }
  return character;
}

bool StartsWithInsensitive(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) return false;
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    if (LowerAscii(value[index]) != LowerAscii(prefix[index])) return false;
  }
  return true;
}

std::size_t FindInsensitive(std::string_view value, std::string_view needle,
                            std::size_t offset = 0) {
  if (needle.empty() || offset > value.size() ||
      needle.size() > value.size() - offset) {
    return std::string_view::npos;
  }
  const std::size_t last = value.size() - needle.size();
  for (std::size_t index = offset; index <= last; ++index) {
    if (StartsWithInsensitive(value.substr(index), needle)) return index;
  }
  return std::string_view::npos;
}

std::size_t TagEnd(std::string_view html, std::size_t begin) {
  char quote = '\0';
  for (std::size_t index = begin; index < html.size(); ++index) {
    const char character = html[index];
    if (quote != '\0') {
      if (character == quote) quote = '\0';
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
    } else if (character == '>') {
      return index;
    }
  }
  return std::string_view::npos;
}

std::string DecodeEntities(std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t index = 0; index < value.size();) {
    if (value[index] != '&') {
      decoded.push_back(value[index++]);
      continue;
    }
    const std::size_t semicolon = value.find(';', index + 1);
    if (semicolon == std::string_view::npos || semicolon - index > 12) {
      decoded.push_back(value[index++]);
      continue;
    }
    const std::string_view entity = value.substr(index + 1,
                                                  semicolon - index - 1);
    if (entity == "amp") {
      decoded.push_back('&');
    } else if (entity == "quot") {
      decoded.push_back('"');
    } else if (entity == "apos" || entity == "#39") {
      decoded.push_back('\'');
    } else if (entity == "nbsp" || entity == "#160" || entity == "#xA0" ||
               entity == "#xa0") {
      decoded.push_back(' ');
    } else if (!entity.empty() && entity.front() == '#') {
      unsigned int codepoint = 0;
      std::string_view digits = entity.substr(1);
      int base = 10;
      if (!digits.empty() && (digits.front() == 'x' || digits.front() == 'X')) {
        digits.remove_prefix(1);
        base = 16;
      }
      const auto parsed = std::from_chars(digits.data(),
                                          digits.data() + digits.size(),
                                          codepoint, base);
      if (parsed.ec == std::errc() &&
          parsed.ptr == digits.data() + digits.size() && codepoint <= 0x7f) {
        decoded.push_back(codepoint == 0x09 || codepoint == 0x0a ||
                                  codepoint == 0x0d || codepoint == 0x20
                              ? ' '
                              : static_cast<char>(codepoint));
      } else {
        decoded.append(value.substr(index, semicolon - index + 1));
      }
    } else {
      decoded.append(value.substr(index, semicolon - index + 1));
    }
    index = semicolon + 1;
  }
  return decoded;
}

std::string CollapseWhitespace(std::string_view value) {
  std::string output;
  bool pending_space = false;
  for (unsigned char character : value) {
    if (std::isspace(character)) {
      pending_space = !output.empty();
      continue;
    }
    if (pending_space) output.push_back(' ');
    pending_space = false;
    output.push_back(static_cast<char>(character));
  }
  return output;
}

void AppendText(std::string* output, std::string_view value) {
  const std::string collapsed = CollapseWhitespace(DecodeEntities(value));
  if (collapsed.empty()) return;
  if (!output->empty()) output->push_back(' ');
  output->append(collapsed);
}

std::string AttributeValue(std::string_view tag, std::string_view wanted) {
  std::size_t index = 1;
  while (index < tag.size() &&
         !std::isspace(static_cast<unsigned char>(tag[index])) &&
         tag[index] != '>' && tag[index] != '/') {
    ++index;
  }
  while (index < tag.size()) {
    while (index < tag.size() &&
           (std::isspace(static_cast<unsigned char>(tag[index])) ||
            tag[index] == '/')) {
      ++index;
    }
    const std::size_t name_begin = index;
    while (index < tag.size() &&
           !std::isspace(static_cast<unsigned char>(tag[index])) &&
           tag[index] != '=' && tag[index] != '>' && tag[index] != '/') {
      ++index;
    }
    if (name_begin == index) break;
    std::string name(tag.substr(name_begin, index - name_begin));
    std::transform(name.begin(), name.end(), name.begin(), LowerAscii);
    while (index < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[index]))) {
      ++index;
    }
    if (index >= tag.size() || tag[index] != '=') continue;
    ++index;
    while (index < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[index]))) {
      ++index;
    }
    if (index >= tag.size()) break;
    const char quote = tag[index] == '\'' || tag[index] == '"'
                           ? tag[index++]
                           : '\0';
    const std::size_t value_begin = index;
    while (index < tag.size() &&
           (quote != '\0' ? tag[index] != quote
                           : !std::isspace(static_cast<unsigned char>(tag[index])) &&
                                 tag[index] != '>')) {
      ++index;
    }
    const std::string_view raw = tag.substr(value_begin, index - value_begin);
    if (quote != '\0' && index < tag.size()) ++index;
    if (name == wanted) return DecodeEntities(raw);
  }
  return {};
}

std::string MarkupText(std::string_view markup) {
  std::string output;
  std::size_t index = 0;
  while (index < markup.size()) {
    if (markup[index] != '<') {
      const std::size_t end = markup.find('<', index);
      const std::size_t stop = end == std::string_view::npos ? markup.size() : end;
      AppendText(&output, markup.substr(index, stop - index));
      index = stop;
      continue;
    }
    const std::size_t end = TagEnd(markup, index);
    if (end == std::string_view::npos) break;
    index = end + 1;
  }
  return output;
}

HtmlDocument ParseHtml(std::string_view html) {
  HtmlDocument document;
  std::size_t index = 0;
  while (index < html.size()) {
    if (html[index] != '<') {
      const std::size_t end = html.find('<', index);
      const std::size_t stop = end == std::string_view::npos ? html.size() : end;
      AppendText(&document.visible_text, html.substr(index, stop - index));
      index = stop;
      continue;
    }
    if (html.substr(index, 4) == "<!--") {
      const std::size_t end = html.find("-->", index + 4);
      index = end == std::string_view::npos ? html.size() : end + 3;
      continue;
    }
    const std::size_t end = TagEnd(html, index);
    if (end == std::string_view::npos) break;
    const std::string_view tag = html.substr(index, end - index + 1);
    const bool closing = tag.size() > 1 && tag[1] == '/';
    std::size_t name_begin = closing ? 2 : 1;
    while (name_begin < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[name_begin]))) {
      ++name_begin;
    }
    std::size_t name_end = name_begin;
    while (name_end < tag.size() &&
           (std::isalnum(static_cast<unsigned char>(tag[name_end])) ||
            tag[name_end] == '-')) {
      ++name_end;
    }
    std::string name(tag.substr(name_begin, name_end - name_begin));
    std::transform(name.begin(), name.end(), name.begin(), LowerAscii);
    if (!closing && name == "meta") {
      std::string key = AttributeValue(tag, "name");
      if (key.empty()) key = AttributeValue(tag, "property");
      std::transform(key.begin(), key.end(), key.begin(), LowerAscii);
      if (key == "description" || key == "og:description") {
        document.version_descriptions.emplace_back(
            key, AttributeValue(tag, "content"));
      }
    }
    if (!closing && (name == "script" || name == "style")) {
      const std::string close_tag = "</" + name;
      const std::size_t close_begin = FindInsensitive(html, close_tag, end + 1);
      if (close_begin == std::string_view::npos) break;
      const std::size_t close_end = TagEnd(html, close_begin);
      index = close_end == std::string_view::npos ? html.size() : close_end + 1;
      continue;
    }
    if (!closing && name == "a") {
      const std::size_t close_begin = FindInsensitive(html, "</a", end + 1);
      if (close_begin == std::string_view::npos) {
        index = end + 1;
        continue;
      }
      const std::size_t close_end = TagEnd(html, close_begin);
      if (close_end == std::string_view::npos) break;
      const std::string link_text =
          MarkupText(html.substr(end + 1, close_begin - end - 1));
      AppendText(&document.visible_text, link_text);
      const std::string href = AttributeValue(tag, "href");
      if (!href.empty()) document.links.push_back({href, link_text});
      index = close_end + 1;
      continue;
    }
    index = end + 1;
  }
  return document;
}

bool ValidVersion(std::string_view version) {
  return !version.empty() && version.size() <= 128 &&
         std::any_of(version.begin(), version.end(), [](unsigned char value) {
           return std::isdigit(value);
         }) &&
         version.find('.') != std::string_view::npos &&
         std::all_of(version.begin(), version.end(), [](unsigned char value) {
           return std::isalnum(value) || value == '.' || value == '+' ||
                  value == '_' || value == '-';
         });
}

std::optional<std::string> ResolvePageHref(std::string_view href) {
  if (href.empty() || href.size() > 4096) return std::nullopt;
  if (href.front() == '/' && (href.size() == 1 || href[1] != '/')) {
    return "https://apkcombo.com" + std::string(href);
  }
  constexpr std::string_view https = "https://";
  if (StartsWithInsensitive(href, https)) {
    std::string error;
    if (IsTrustedHttpsUrl(href, {std::string(kApkComboHost)}, &error, true)) {
      return std::string(href);
    }
  }
  return std::nullopt;
}

std::optional<std::string> UrlPath(std::string_view url) {
  const std::size_t scheme = url.find("://");
  if (scheme == std::string_view::npos) return std::nullopt;
  const std::size_t authority = scheme + 3;
  const std::size_t slash = url.find('/', authority);
  if (slash == std::string_view::npos) return std::string("/");
  const std::size_t end = url.find_first_of("?#", slash);
  return std::string(url.substr(
      slash, end == std::string_view::npos ? url.size() - slash : end - slash));
}

bool HasQueryOrFragment(std::string_view url) {
  return url.find_first_of("?#") != std::string_view::npos;
}

std::optional<std::string> LatestVersionFromProductPage(
    std::string_view html) {
  const HtmlDocument document = ParseHtml(html);
  std::optional<std::string> description_version;
  std::optional<std::string> open_graph_version;
  for (const auto& [kind, description] : document.version_descriptions) {
    const std::string marker = "Download Roblox APK ";
    const std::size_t match = FindInsensitive(description, marker);
    if (match == std::string::npos ||
        FindInsensitive(description, marker, match + marker.size()) !=
            std::string::npos) {
      return std::nullopt;
    }
    std::size_t begin = match + marker.size();
    std::size_t end = begin;
    while (end < description.size()) {
      const unsigned char character =
          static_cast<unsigned char>(description[end]);
      if (!std::isalnum(character) && description[end] != '.' &&
          description[end] != '+' && description[end] != '_' &&
          description[end] != '-') {
        break;
      }
      ++end;
    }
    const std::string version = description.substr(begin, end - begin);
    if (!ValidVersion(version)) return std::nullopt;
    std::optional<std::string>* slot =
        kind == "description" ? &description_version : &open_graph_version;
    if (slot->has_value()) return std::nullopt;
    *slot = version;
  }
  if (!description_version.has_value() || !open_graph_version.has_value() ||
      *description_version != *open_graph_version) {
    return std::nullopt;
  }

  std::size_t matching_version_links = 0;
  const std::string selected_path =
      std::string(kVersionPathPrefix) + *description_version +
      std::string(kVersionPathSuffix);
  for (const HtmlLink& link : document.links) {
    const auto url = ResolvePageHref(link.href);
    if (!url.has_value() || HasQueryOrFragment(*url)) continue;
    const auto path = UrlPath(*url);
    if (path.has_value() && *path == selected_path) {
      ++matching_version_links;
    }
  }
  return matching_version_links == 1 ? description_version : std::nullopt;
}

std::optional<std::uint64_t> VersionCodeFromText(std::string_view text,
                                                std::string_view version) {
  const std::string marker = "Roblox " + std::string(version);
  std::optional<std::uint64_t> found;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::size_t match = text.find(marker, offset);
    if (match == std::string_view::npos) break;
    std::size_t cursor = match + marker.size();
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor]))) {
      ++cursor;
    }
    if (cursor >= text.size() || text[cursor] != '(') {
      offset = match + marker.size();
      continue;
    }
    ++cursor;
    const std::size_t code_begin = cursor;
    while (cursor < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[cursor])) &&
           cursor - code_begin <= 20) {
      ++cursor;
    }
    if (cursor == code_begin || cursor >= text.size() || text[cursor] != ')') {
      offset = match + marker.size();
      continue;
    }
    std::uint64_t code = 0;
    const auto parsed = std::from_chars(text.data() + code_begin,
                                        text.data() + cursor, code);
    if (parsed.ec == std::errc() && parsed.ptr == text.data() + cursor &&
        code > 0) {
      if (found.has_value() && *found != code) return std::nullopt;
      found = code;
    }
    offset = cursor + 1;
  }
  return found;
}

std::optional<std::string> ArtifactHref(const HtmlDocument& document) {
  std::optional<std::string> artifact;
  for (const HtmlLink& link : document.links) {
    const auto url = ResolvePageHref(link.href);
    if (!url.has_value()) continue;
    const auto path = UrlPath(*url);
    if (!path.has_value() || *path != "/r2") continue;
    const std::size_t query = url->find('?');
    if (query == std::string::npos || query + 1 >= url->size() ||
        url->find('#') != std::string::npos) {
      return std::nullopt;
    }
    std::string label = link.text;
    std::transform(label.begin(), label.end(), label.begin(), LowerAscii);
    if (label.find("xapk") == std::string::npos || artifact.has_value()) {
      return std::nullopt;
    }
    artifact = *url;
  }
  return artifact;
}

std::string VersionPageUrl(std::string_view version) {
  return std::string(kVersionedPagePrefix) + std::string(version) +
         std::string(kVersionedPageSuffix);
}

HttpBytesResult FetchPage(std::string_view url,
                          std::string_view expected_path) {
  HttpTransferRequest request;
  request.url = std::string(url);
  request.allowed_hosts = {std::string(kApkComboHost)};
  request.exact_host_matches = true;
  request.maximum_bytes = kMaximumMetadataBytes;
  request.connect_timeout_ms = 10000;
  request.transfer_timeout_ms = 20000;
  request.total_timeout_ms = 20000;
  request.maximum_attempts = 1;
  request.maximum_redirects = 2;
  request.forbidden_paths = {"/downloader", "/checkin"};
  HttpBytesResult result = DownloadBytes(request);
  if (!result) {
    result.bytes.clear();
    result.final_url.clear();
    result.error = "APKCombo page request failed";
    return result;
  }
  const auto final_path = UrlPath(result.final_url);
  if (!final_path.has_value() || *final_path != expected_path ||
      HasQueryOrFragment(result.final_url)) {
    result.bytes.clear();
    result.final_url.clear();
    result.error = "APKCombo returned an unexpected page";
  }
  return result;
}

ParsedRelease ParseVersionPage(std::string_view html,
                               std::string_view expected_version) {
  ParsedRelease result;
  result.version.version_name = std::string(expected_version);
  const HtmlDocument document = ParseHtml(html);
  if (html.find("com.roblox.client") == std::string_view::npos ||
      document.visible_text.find("x86_64") == std::string::npos) {
    result.version.error = "APKCombo page omits the expected Roblox x86_64 listing";
    return result;
  }
  const std::string expected_path =
      std::string(kVersionPathPrefix) + std::string(expected_version) +
      std::string(kVersionPathSuffix);
  bool exact_version_page = false;
  for (const HtmlLink& link : document.links) {
    const auto url = ResolvePageHref(link.href);
    if (!url.has_value() || HasQueryOrFragment(*url)) continue;
    const auto path = UrlPath(*url);
    if (path.has_value() && *path == expected_path) {
      exact_version_page = true;
      break;
    }
  }
  if (!exact_version_page && html.find(expected_path) == std::string_view::npos) {
    result.version.error = "APKCombo page does not match the requested version";
    return result;
  }
  const auto code = VersionCodeFromText(document.visible_text, expected_version);
  if (!code.has_value()) {
    result.version.error = "APKCombo page omits an unambiguous version code";
    return result;
  }
  const auto artifact = ArtifactHref(document);
  if (!artifact.has_value()) {
    result.version.error = "APKCombo page omits one unambiguous XAPK link";
    return result;
  }
  result.version.version_code = *code;
  result.artifact_url = *artifact;
  return result;
}

ProviderVersion FetchLatestRelease() {
  ProviderVersion failure;
  const HttpBytesResult product = FetchPage(kProductUrl, kProductPath);
  if (!product) {
    failure.error = product.error;
    return failure;
  }
  const auto version = LatestVersionFromProductPage(product.bytes);
  if (!version.has_value()) {
    failure.error = "APKCombo product page omits a valid latest version";
    return failure;
  }
  const std::string page_url = VersionPageUrl(*version);
  const std::string page_path =
      std::string(kVersionPathPrefix) + *version +
      std::string(kVersionPathSuffix);
  const HttpBytesResult version_page = FetchPage(page_url, page_path);
  if (!version_page) {
    failure.error = version_page.error;
    return failure;
  }
  return ParseVersionPage(version_page.bytes, *version).version;
}

bool ZipMagic(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  char magic[4] = {};
  input.read(magic, sizeof(magic));
  return input.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' &&
         (magic[2] == 3 || magic[2] == 5 || magic[2] == 7) &&
         (magic[3] == 4 || magic[3] == 6 || magic[3] == 8);
}

}  // namespace

ProviderVersion ApkComboProvider::CheckLatest() const {
  return FetchLatestRelease();
}

ProviderDownloadResult ApkComboProvider::DownloadExact(
    std::string_view version, const std::filesystem::path& output_directory,
    int progress_fd) const {
  ProviderDownloadResult result;
  result.source = std::string(name());
  if (!ValidVersion(version)) {
    result.error = "invalid APKCombo version";
    return result;
  }
  std::error_code filesystem_error;
  std::filesystem::create_directories(output_directory, filesystem_error);
  if (filesystem_error ||
      !std::filesystem::is_empty(output_directory, filesystem_error)) {
    result.error = "APKCombo output directory must exist and be empty";
    return result;
  }

  const std::string page_url = VersionPageUrl(version);
  const std::string page_path =
      std::string(kVersionPathPrefix) + std::string(version) +
      std::string(kVersionPathSuffix);
  const HttpBytesResult version_page = FetchPage(page_url, page_path);
  if (!version_page) {
    result.error = version_page.error;
    return result;
  }
  ParsedRelease release = ParseVersionPage(version_page.bytes, version);
  if (!release.version) {
    result.error = release.version.error;
    return result;
  }

  const std::filesystem::path archive = output_directory / "candidate.xapk";
  HttpTransferRequest request;
  request.url = release.artifact_url;
  request.allowed_hosts = {std::string(kApkComboHost),
                           std::string(kArtifactHost)};
  request.exact_host_matches = true;
  request.forbidden_paths = {"/downloader", "/checkin"};
  request.maximum_bytes = kMaximumArchiveBytes;
  request.connect_timeout_ms = 10000;
  request.transfer_timeout_ms = 30L * 60L * 1000L;
  request.total_timeout_ms = 30L * 60L * 1000L;
  request.maximum_attempts = 2;
  request.retry_delay_ms = 1000;
  request.maximum_redirects = 2;
  HttpDownloadResult downloaded =
      DownloadFile(request, archive, progress_fd);
  request.url.clear();
  release.artifact_url.clear();
  const bool valid_archive = downloaded && ZipMagic(archive);
  downloaded.final_url.clear();
  if (!valid_archive) {
    std::filesystem::remove_all(output_directory, filesystem_error);
    // Transport diagnostics can contain remote URLs. Keep them out of user
    // messages, receipts, and logs; all downloaded bytes remain untrusted.
    result.error = downloaded ? "APKCombo response is not a ZIP archive"
                              : "APKCombo XAPK download failed";
    return result;
  }
  result.archives.push_back(archive);
  return result;
}

}  // namespace aurora::update
