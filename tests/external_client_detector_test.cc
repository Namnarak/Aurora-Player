// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include "runtime/external_client_detector.h"

#include <gtest/gtest.h>

#include <string>

namespace aurora {
namespace runtime {
namespace {

TEST(ExternalClientDetectorTest, ClassifiesClaudeDesktopBeforeCli) {
  const auto client = ExternalClientDetector::ClassifyProcess(
      "claude-desktop", "/usr/lib/claude-desktop/claude-desktop\0--hidden",
      "/usr/lib/claude-desktop/claude-desktop");
  ASSERT_TRUE(client.has_value());
  EXPECT_EQ(*client, DetectedClient::kClaudeDesktop);
}

TEST(ExternalClientDetectorTest, ClassifiesClaudeCodeCli) {
  const auto client = ExternalClientDetector::ClassifyProcess(
      "claude", "/home/test/.config/Claude/claude-code/2.1.0/claude\0--json",
      "/home/test/.config/Claude/claude-code/2.1.0/claude");
  ASSERT_TRUE(client.has_value());
  EXPECT_EQ(*client, DetectedClient::kClaudeCode);
}

TEST(ExternalClientDetectorTest, ClassifiesRobloxQuickConnectClients) {
  struct Case {
    const char* comm;
    const char* cmdline;
    const char* executable;
    DetectedClient expected;
  };
  const Case cases[] = {
      {"antigravity", "/opt/Antigravity/antigravity\0", "/opt/Antigravity/antigravity",
       DetectedClient::kAntigravity},
      {"cursor", "/opt/Cursor/cursor\0", "/opt/Cursor/cursor",
       DetectedClient::kCursor},
      {"gemini", "/home/test/.local/bin/gemini\0", "/home/test/.local/bin/gemini",
       DetectedClient::kGeminiCli},
      {"code", "/usr/share/code/code\0--ms-enable-electron-run-as-node",
       "/usr/share/code/code", DetectedClient::kVisualStudioCode},
  };
  for (const Case& test_case : cases) {
    const auto client = ExternalClientDetector::ClassifyProcess(
        test_case.comm, test_case.cmdline, test_case.executable);
    ASSERT_TRUE(client.has_value()) << test_case.executable;
    EXPECT_EQ(*client, test_case.expected) << test_case.executable;
  }
}

TEST(ExternalClientDetectorTest, ClassifiesCodexDesktopHelperAsDesktop) {
  const auto client = ExternalClientDetector::ClassifyProcess(
      "codex", "/opt/codex-desktop/resources/codex\0app-server",
      "/opt/codex-desktop/resources/codex");
  ASSERT_TRUE(client.has_value());
  EXPECT_EQ(*client, DetectedClient::kCodexDesktop);
}

TEST(ExternalClientDetectorTest, ClassifiesCodexCli) {
  const auto client = ExternalClientDetector::ClassifyProcess(
      "codex", "/home/test/.local/bin/codex\0--version",
      "/home/test/.local/bin/codex");
  ASSERT_TRUE(client.has_value());
  EXPECT_EQ(*client, DetectedClient::kCodexCli);
}

TEST(ExternalClientDetectorTest, ClassifiesStudioMcp) {
  const auto client = ExternalClientDetector::ClassifyProcess(
      "wine", "wine\0StudioMCP.exe\0", "/tmp/StudioMCP.exe");
  ASSERT_TRUE(client.has_value());
  EXPECT_EQ(*client, DetectedClient::kRobloxStudioMcp);
}

TEST(ExternalClientDetectorTest, IgnoresUnrelatedProcess) {
  const auto client = ExternalClientDetector::ClassifyProcess(
      "firefox", "/usr/lib/firefox/firefox\0", "/usr/lib/firefox/firefox");
  EXPECT_FALSE(client.has_value());
}

TEST(ExternalClientDetectorTest, ExposesStableClientMetadata) {
  EXPECT_STREQ(ExternalClientDetector::Id(DetectedClient::kClaudeDesktop),
               "claude_desktop");
  EXPECT_STREQ(ExternalClientDetector::Name(DetectedClient::kClaudeCode),
               "Claude Code");
  EXPECT_STREQ(ExternalClientDetector::Kind(DetectedClient::kCodexCli),
               "ai_cli");
}

}  // namespace
}  // namespace runtime
}  // namespace aurora
