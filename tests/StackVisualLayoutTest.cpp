#include <cmath>
#include <cstdint>
#include <iostream>

#if __has_include("StackVisualLayout.hpp")
#include "StackVisualLayout.hpp"
#else
int main() {
  std::cerr << "StackVisualLayout.hpp is missing\n";
  return 1;
}
#endif

#if __has_include("StackVisualLayout.hpp")
namespace {

bool closeEnough(float actual, float expected) {
  return std::abs(actual - expected) < 0.00001f;
}

int fail(const char *message) {
  std::cerr << message << '\n';
  return 1;
}

} // namespace

int main() {
  using itemphysics::compactGridOffset;
  using itemphysics::rotateStackOffset;
  using itemphysics::selectVisualCopyCount;

  struct CopyCase {
    std::uint32_t count;
    bool singleModel;
    bool realItemModels;
    std::uint32_t expected;
  };
  constexpr CopyCase cases[] = {
      {0, false, false, 1},  {1, false, false, 1},
      {2, false, false, 2},  {16, false, false, 2},
      {17, false, false, 3}, {32, false, false, 3},
      {33, false, false, 4}, {48, false, false, 4},
      {49, false, false, 5}, {64, false, false, 5},
      {2, false, true, 2},   {17, false, true, 17},
      {64, false, true, 64}, {255, false, true, 64},
      {64, true, true, 1},   {64, true, false, 1},
  };

  for (const auto &test : cases) {
    const auto actual =
        selectVisualCopyCount(test.count, test.singleModel,
                              test.realItemModels);
    if (actual != test.expected)
      return fail("visual copy-count policy returned the wrong value");
  }

  constexpr float step = 0.1f;
  const auto p0 = compactGridOffset(0, 4, step);
  const auto p1 = compactGridOffset(1, 4, step);
  const auto p2 = compactGridOffset(2, 4, step);
  const auto p3 = compactGridOffset(3, 4, step);
  if (!closeEnough(p0.x, -0.05f) || !closeEnough(p0.z, -0.05f) ||
      !closeEnough(p1.x, 0.05f) || !closeEnough(p1.z, -0.05f) ||
      !closeEnough(p2.x, -0.05f) || !closeEnough(p2.z, 0.05f) ||
      !closeEnough(p3.x, 0.05f) || !closeEnough(p3.z, 0.05f))
    return fail("four exact copies are not a centred two-by-two XZ grid");

  const auto yaw0 = rotateStackOffset(p0, 0.0f, 1.0f);
  if (!closeEnough(yaw0.x, p0.x) || !closeEnough(yaw0.z, p0.z))
    return fail("zero-yaw stack rotation changed the local XZ offset");

  const auto yawQuarter = rotateStackOffset({1.0f, 0.0f}, 1.0f, 0.0f);
  if (!closeEnough(yawQuarter.x, 0.0f) ||
      !closeEnough(yawQuarter.z, 1.0f))
    return fail("quarter-yaw stack rotation did not remain in world XZ");

  for (std::uint32_t i = 0; i < 64; ++i) {
    const auto offset = compactGridOffset(i, 64, step);
    if (std::abs(offset.x) > 0.35001f || std::abs(offset.z) > 0.35001f)
      return fail("64-copy grid exceeded its compact eight-by-eight bounds");
    for (std::uint32_t j = 0; j < i; ++j) {
      const auto previous = compactGridOffset(j, 64, step);
      if (closeEnough(offset.x, previous.x) &&
          closeEnough(offset.z, previous.z))
        return fail("two exact copies received the same XZ position");
    }
  }

  return 0;
}
#endif
