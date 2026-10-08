#include <cstddef>
#include <cstdlib>

extern "C" {

__attribute__((visibility("default"), noinline, used)) void*
AuroraFixtureSmallAllocate(std::size_t size) {
  return std::malloc(size);
}

__attribute__((visibility("default"), noinline, used)) void*
AuroraFixtureAllocate(std::size_t size) {
  return std::malloc(size);
}

__attribute__((visibility("default"), noinline, used)) void*
AuroraFixtureReallocate(void* pointer, std::size_t size) {
  return std::realloc(pointer, size);
}

__attribute__((visibility("default"), noinline, used)) void*
AuroraFixtureAlignedAllocate(std::size_t alignment, std::size_t size) {
  void* pointer = nullptr;
  return posix_memalign(&pointer, alignment, size) == 0 ? pointer : nullptr;
}

__attribute__((visibility("default"), noinline, used)) void AuroraFixtureFree(
    void* pointer) {
  std::free(pointer);
}

__attribute__((visibility("default"), noinline, used)) std::size_t
AuroraFixtureUsableSize(void*) {
  return 1;
}

__attribute__((visibility("default"), noinline, used)) void
AuroraFixtureArenaInitialize() {}

__attribute__((visibility("default"), noinline, used)) void
AuroraFixtureThreadInitialize() {}

__attribute__((visibility("default"), noinline, used)) void
AuroraFixtureRegistryInitialize() {}

__attribute__((visibility("default"), constructor(101), used)) void
AuroraFixtureConstructorTwo() {}

__attribute__((visibility("default"), constructor(102), used)) void
AuroraFixtureConstructorThree() {}

__attribute__((visibility("default"), constructor(103), used)) void
AuroraFixtureConstructorFour() {}

__attribute__((visibility("default"), constructor(104), used)) void
AuroraFixtureConstructorFive() {}

__attribute__((visibility("default"),
               used)) void* aurora_fixture_allocator_slot = nullptr;
__attribute__((visibility("default"),
               used)) void* aurora_fixture_empty_string_slot = nullptr;
__attribute__((visibility("default"), used)) void* aurora_fixture_jni_slot =
    nullptr;
__attribute__((visibility("default"),
               used)) void* aurora_fixture_arena_guard_slot = nullptr;
__attribute__((visibility("default"),
               used)) void* aurora_fixture_arena_table_slot = nullptr;
__attribute__((visibility("default"),
               used)) void* aurora_fixture_registry_slot = nullptr;

}  // extern "C"

namespace {

struct FixtureConstructor {
  FixtureConstructor() { aurora_fixture_arena_guard_slot = nullptr; }
};

FixtureConstructor g_fixture_constructor;

}  // namespace
