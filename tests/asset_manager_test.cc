#include "libc_shim/vulkan_etc1_sky_transcoder.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

struct AAssetManager;
struct AAsset;

extern "C" {
AAssetManager* AAssetManager_fromJava(void* env, void* assetManager);
AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode);
void AAsset_close(AAsset* asset);
int AAsset_read(AAsset* asset, void* buf, std::size_t count);
const void* AAsset_getBuffer(AAsset* asset);
off_t AAsset_getLength(AAsset* asset);
off_t AAsset_getRemainingLength(AAsset* asset);
off_t AAsset_seek(AAsset* asset, off_t offset, int whence);
int AAsset_openFileDescriptor(AAsset* asset, off_t* outStart,
                              off_t* outLength);
int AAsset_openFileDescriptor64(AAsset* asset, off_t* outStart,
                                off_t* outLength);
}

namespace {

std::vector<unsigned char> MakeEtc1SkyTexture() {
  std::vector<unsigned char> data = {
      0xab, 0x4b, 0x54, 0x58, 0x20, 0x31, 0x31, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a,
  };
  // KTX1: one 4x4 ETC1 mip and its byte count.
  for (uint32_t value : {0x04030201U, 0U, 1U, 0U, 0x8d64U, 0x1907U,
                         4U, 4U, 0U, 0U, 1U, 1U, 0U, 8U}) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      data.push_back(static_cast<unsigned char>(value >> shift));
    }
  }
  const std::array<unsigned char, 8> block = {0x55, 0x77, 0x99, 0, 0, 0, 0, 0};
  data.insert(data.end(), block.begin(), block.end());
  return data;
}

std::vector<unsigned char> MakeEtc1SkyTextureWithTruncatedSecondMip() {
  std::vector<unsigned char> data = MakeEtc1SkyTexture();
  data[56] = 2;
  data[57] = 0;
  data[58] = 0;
  data[59] = 0;
  constexpr uint32_t second_mip_size = 8;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    data.push_back(static_cast<unsigned char>(second_mip_size >> shift));
  }
  const std::array<unsigned char, 4> truncated_block = {
      0x11, 0x22, 0x33, 0x44};
  data.insert(data.end(), truncated_block.begin(), truncated_block.end());
  return data;
}

class AssetManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = MakeTempRoot();
    ASSERT_FALSE(root_.empty());
    setenv("AURORA_ASSET_ROOT", root_.c_str(), 1);
  }

  void TearDown() override {
    unsetenv("AURORA_ASSET_ROOT");
    for (const std::string& root : roots_to_remove_) {
      std::filesystem::remove_all(root);
    }
  }

  std::string MakeTempRoot() {
    char path[] = "/tmp/aurora-assets-XXXXXX";
    char* dir = mkdtemp(path);
    if (dir == nullptr) {
      return {};
    }
    roots_to_remove_.emplace_back(dir);
    return dir;
  }

  void WriteAsset(const std::string& name, const char* data) {
    WriteAssetAt(root_, name, data);
  }

  void WriteAssetAt(const std::string& root, const std::string& name,
                    const char* data) {
    std::string path = root + "/" + name;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(data, 1, std::strlen(data), file),
              std::strlen(data));
    ASSERT_EQ(std::fclose(file), 0);
  }

  void WriteBinaryAsset(const std::string& name,
                         const std::vector<unsigned char>& data) {
    const std::filesystem::path path = std::filesystem::path(root_) / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    ASSERT_TRUE(file);
  }

  std::string root_;
  std::vector<std::string> roots_to_remove_;
  bool had_backend_ = false;
  std::string previous_backend_;
};

TEST_F(AssetManagerTest, FromJavaReturnsSingletonManager) {
  EXPECT_NE(AAssetManager_fromJava(nullptr, nullptr), nullptr);
}

TEST_F(AssetManagerTest, ConvertsSolidEtc1RgbBlockToOpaqueBc1) {
  const std::array<uint8_t, 8> etc1 = {0x55, 0x77, 0x99, 0, 0, 0, 0, 0};
  std::array<uint8_t, 8> bc1{};

  libc_shim::ConvertEtc1RgbBlockToBc1(etc1.data(), bc1.data());

  const uint16_t endpoint0 = static_cast<uint16_t>(bc1[0] | (bc1[1] << 8));
  const uint16_t endpoint1 = static_cast<uint16_t>(bc1[2] | (bc1[3] << 8));
  EXPECT_GT(endpoint0, endpoint1);
}

TEST_F(AssetManagerTest, OpenReadSeekAndBufferUseConfiguredAssetRoot) {
  WriteAsset("config.json", "aurora-assets");

  AAssetManager* manager = AAssetManager_fromJava(nullptr, nullptr);
  AAsset* asset = AAssetManager_open(manager, "config.json", 0);
  ASSERT_NE(asset, nullptr);

  EXPECT_EQ(AAsset_getLength(asset), 13);
  char first[5] = {};
  EXPECT_EQ(AAsset_read(asset, first, 4), 4);
  EXPECT_STREQ(first, "auro");
  EXPECT_EQ(AAsset_getRemainingLength(asset), 9);

  EXPECT_EQ(AAsset_seek(asset, 7, SEEK_SET), 7);
  char second[7] = {};
  EXPECT_EQ(AAsset_read(asset, second, 6), 6);
  EXPECT_STREQ(second, "assets");

  const auto* buffer = static_cast<const char*>(AAsset_getBuffer(asset));
  ASSERT_NE(buffer, nullptr);
  EXPECT_EQ(std::string(buffer, buffer + AAsset_getLength(asset)),
            "aurora-assets");

  AAsset_close(asset);
}

TEST_F(AssetManagerTest, OpensExtraContentModelsFromRbxAssetPaths) {
  WriteAsset("ExtraContent/models/UniversalApp/UniversalApp.rbxm", "rbxm");

  AAssetManager* manager = AAssetManager_fromJava(nullptr, nullptr);
  AAsset* stripped =
      AAssetManager_open(manager, "models/UniversalApp/UniversalApp.rbxm", 0);
  ASSERT_NE(stripped, nullptr);
  EXPECT_EQ(AAsset_getLength(stripped), 4);
  AAsset_close(stripped);

  AAsset* uri = AAssetManager_open(
      manager, "rbxasset://models/UniversalApp/UniversalApp.rbxm", 0);
  ASSERT_NE(uri, nullptr);
  EXPECT_EQ(AAsset_getLength(uri), 4);
  AAsset_close(uri);
}

TEST_F(AssetManagerTest, OpenFileDescriptorReturnsReadableFd) {
  WriteAsset("config.json", "fd-data");

  AAsset* asset =
      AAssetManager_open(AAssetManager_fromJava(nullptr, nullptr),
                         "assets/config.json", 0);
  ASSERT_NE(asset, nullptr);

  off_t start = -1;
  off_t length = -1;
  int fd = AAsset_openFileDescriptor(asset, &start, &length);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(start, 0);
  EXPECT_EQ(length, 7);

  char buf[8] = {};
  EXPECT_EQ(read(fd, buf, sizeof(buf) - 1), 7);
  EXPECT_STREQ(buf, "fd-data");
  close(fd);
  AAsset_close(asset);
}

TEST_F(AssetManagerTest, TranscodedSkyCannotBypassConversionThroughDescriptor) {
  const auto original = MakeEtc1SkyTexture();
  const std::string name = "assets/android/textures/sky/test.tex";
  WriteBinaryAsset(name, original);
  ASSERT_EQ(setenv("AURORA_GRAPHICS_BACKEND", "direct-vulkan", 1), 0);

  AAsset* asset = AAssetManager_open(
      AAssetManager_fromJava(nullptr, nullptr), name.c_str(), 0);
  ASSERT_NE(asset, nullptr);
  ASSERT_EQ(AAsset_getLength(asset), static_cast<off_t>(original.size()));
  const auto* buffer = static_cast<const unsigned char*>(AAsset_getBuffer(asset));
  ASSERT_NE(buffer, nullptr);
  // BC1 format in the KTX header.
  EXPECT_EQ(buffer[28], 0xf0);
  EXPECT_EQ(buffer[29], 0x83);

  for (auto open_descriptor : {AAsset_openFileDescriptor,
                               AAsset_openFileDescriptor64}) {
    off_t start = -1;
    off_t length = -1;
    const int fd = open_descriptor(asset, &start, &length);
    EXPECT_LT(fd, 0);
    if (fd >= 0) close(fd);
  }

  std::vector<unsigned char> readback(original.size());
  EXPECT_EQ(AAsset_read(asset, readback.data(), readback.size()),
            static_cast<int>(readback.size()));
  EXPECT_EQ(readback, std::vector<unsigned char>(buffer, buffer + readback.size()));
  AAsset_close(asset);

  std::vector<unsigned char> on_disk(original.size());
  std::ifstream file(root_ + "/" + name, std::ios::binary);
  file.read(reinterpret_cast<char*>(on_disk.data()), on_disk.size());
  EXPECT_EQ(on_disk, original);
}

TEST_F(AssetManagerTest, FailedSkyTranscodeLeavesBufferAndDescriptorsUntouched) {
  const auto original = MakeEtc1SkyTextureWithTruncatedSecondMip();
  const std::string name = "assets/android/textures/sky/truncated.tex";
  WriteBinaryAsset(name, original);
  ASSERT_EQ(setenv("AURORA_GRAPHICS_BACKEND", "direct-vulkan", 1), 0);

  AAsset* asset = AAssetManager_open(
      AAssetManager_fromJava(nullptr, nullptr), name.c_str(), 0);
  ASSERT_NE(asset, nullptr);
  ASSERT_EQ(AAsset_getLength(asset), static_cast<off_t>(original.size()));
  const auto* buffer = static_cast<const unsigned char*>(AAsset_getBuffer(asset));
  ASSERT_NE(buffer, nullptr);
  EXPECT_EQ(std::vector<unsigned char>(buffer, buffer + original.size()),
            original);

  std::vector<unsigned char> readback(original.size());
  EXPECT_EQ(AAsset_read(asset, readback.data(), readback.size()),
            static_cast<int>(readback.size()));
  EXPECT_EQ(readback, original);

  for (auto open_descriptor : {AAsset_openFileDescriptor,
                               AAsset_openFileDescriptor64}) {
    off_t start = -1;
    off_t length = -1;
    const int fd = open_descriptor(asset, &start, &length);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(length, static_cast<off_t>(original.size()));
    std::vector<unsigned char> descriptor_bytes(original.size());
    EXPECT_EQ(pread(fd, descriptor_bytes.data(), descriptor_bytes.size(), start),
              static_cast<ssize_t>(descriptor_bytes.size()));
    EXPECT_EQ(descriptor_bytes, original);
    close(fd);
  }
  AAsset_close(asset);
}

TEST_F(AssetManagerTest, OpenGlSkyKeepsOriginalFileDescriptor) {
  const auto original = MakeEtc1SkyTexture();
  const std::string name = "assets/android/textures/sky/test.tex";
  WriteBinaryAsset(name, original);
  ASSERT_EQ(setenv("AURORA_GRAPHICS_BACKEND", "opengl", 1), 0);

  AAsset* asset = AAssetManager_open(
      AAssetManager_fromJava(nullptr, nullptr), name.c_str(), 0);
  ASSERT_NE(asset, nullptr);
  off_t start = -1;
  off_t length = -1;
  const int fd = AAsset_openFileDescriptor64(asset, &start, &length);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(length, static_cast<off_t>(original.size()));
  std::vector<unsigned char> readback(original.size());
  EXPECT_EQ(pread(fd, readback.data(), readback.size(), start),
            static_cast<ssize_t>(readback.size()));
  EXPECT_EQ(readback, original);
  close(fd);
  AAsset_close(asset);
}

TEST_F(AssetManagerTest, MissingOrUnsafeAssetsReturnNull) {
  AAssetManager* manager = AAssetManager_fromJava(nullptr, nullptr);
  EXPECT_EQ(AAssetManager_open(manager, "missing.bin", 0), nullptr);
  EXPECT_EQ(AAssetManager_open(manager, "../config.json", 0), nullptr);
  EXPECT_EQ(AAssetManager_open(manager, "/tmp/config.json", 0), nullptr);
}

TEST_F(AssetManagerTest, MissingAssetCanBeCreatedAndOpenedLater) {
  AAssetManager* manager = AAssetManager_fromJava(nullptr, nullptr);
  EXPECT_EQ(AAssetManager_open(manager, "created-later.txt", 0), nullptr);

  WriteAsset("created-later.txt", "now-present");
  AAsset* asset = AAssetManager_open(manager, "created-later.txt", 0);
  ASSERT_NE(asset, nullptr);
  EXPECT_EQ(AAsset_getLength(asset), 11);
  EXPECT_EQ(std::string(static_cast<const char*>(AAsset_getBuffer(asset)),
                        static_cast<std::size_t>(AAsset_getLength(asset))),
            "now-present");
  AAsset_close(asset);
}

TEST_F(AssetManagerTest, CacheSeparatesRuntimeAssetRootChanges) {
  WriteAsset("same-name.txt", "first-root");
  AAssetManager* manager = AAssetManager_fromJava(nullptr, nullptr);
  AAsset* first = AAssetManager_open(manager, "same-name.txt", 0);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(std::string(static_cast<const char*>(AAsset_getBuffer(first)),
                        static_cast<std::size_t>(AAsset_getLength(first))),
            "first-root");
  AAsset_close(first);

  const std::string second_root = MakeTempRoot();
  ASSERT_FALSE(second_root.empty());
  WriteAssetAt(second_root, "same-name.txt", "second-root");
  ASSERT_EQ(setenv("AURORA_ASSET_ROOT", second_root.c_str(), 1), 0);

  AAsset* second = AAssetManager_open(manager, "same-name.txt", 0);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(std::string(static_cast<const char*>(AAsset_getBuffer(second)),
                        static_cast<std::size_t>(AAsset_getLength(second))),
            "second-root");
  AAsset_close(second);
}

}  // namespace
