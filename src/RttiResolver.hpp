#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace itemphysics {

struct MemoryRange {
  std::uintptr_t begin{};
  std::uintptr_t end{};
  bool readable{};
  bool writable{};
  bool executable{};

  [[nodiscard]] bool contains(std::uintptr_t address,
                              std::size_t size = 1) const noexcept {
    return address >= begin && address <= end && size <= end - address;
  }
};

struct ModuleView {
  static constexpr std::size_t kMaxLoadSegments = 16;

  std::uintptr_t base{};
  std::array<MemoryRange, kMaxLoadSegments> ranges{};
  std::array<std::uint8_t, 20> buildId{};
  std::size_t rangeCount{};
  bool hasBuildId{};

  [[nodiscard]] bool readable(std::uintptr_t,
                              std::size_t size = 1) const noexcept;
  [[nodiscard]] bool executable(std::uintptr_t) const noexcept;
};

struct ResolvedVirtual {
  ModuleView module;
  std::uintptr_t vptr{};
  std::uintptr_t target{};
};

std::optional<ModuleView> findLoadedModule(std::string_view);
std::optional<ResolvedVirtual> resolveVirtualByRtti(std::string_view,
                                                    std::string_view,
                                                    std::size_t);

} // namespace itemphysics
