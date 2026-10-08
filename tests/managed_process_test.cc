// Copyright 2026 NamKrub
// SPDX-License-Identifier: Apache-2.0

#include "runtime/managed_process.h"

#include <gtest/gtest.h>
#include <sys/wait.h>

#include <chrono>
#include <string>

namespace aurora {
namespace runtime {
namespace {

TEST(ManagedProcessTest, ReportsExitAndReapsChild) {
  ManagedProcessSpec spec;
  spec.executable = "/bin/sh";
  spec.arguments = {"-c", "exit 7"};
  ManagedProcess process = ManagedProcess::Start(spec);
  ASSERT_TRUE(process.valid());
  int status = 0;
  ASSERT_TRUE(process.Wait(&status));
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 7);
  EXPECT_FALSE(process.running());
}

TEST(ManagedProcessTest, TerminatesTheOwnedProcessGroup) {
  ManagedProcessSpec spec;
  spec.executable = "/bin/sleep";
  spec.arguments = {"30"};
  spec.inherit_stdio = false;
  ManagedProcess process = ManagedProcess::Start(spec);
  ASSERT_TRUE(process.valid());
  EXPECT_TRUE(process.Terminate(std::chrono::milliseconds(100)));
  EXPECT_FALSE(process.running());
}

TEST(ManagedProcessTest, RejectsMissingExecutableWithoutCreatingAChild) {
  ManagedProcessSpec spec;
  spec.executable = "/definitely/not/an/aurora-executable";
  std::string error;
  ManagedProcess process = ManagedProcess::Start(spec, &error);
  EXPECT_FALSE(process.valid());
  EXPECT_FALSE(process.running());
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace runtime
}  // namespace aurora
