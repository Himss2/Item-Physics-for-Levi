#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

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
  std::uintptr_t base{};
  std::vector<MemoryRange> ranges;

  [[nodiscard]] bool readable(std::uintptr_t address,
                              std::size_t size = 1) const noexcept;
  [[nodiscard]] bool executable(std::uintptr_t address) const noexcept;
};

struct ResolvedVirtual {
  ModuleView module;
  std::uintptr_t vptr{};
  std::uintptr_t target{};
};

std::optional<ModuleView> findLoadedModule(std::string_view basename);
std::optional<ResolvedVirtual>
resolveVirtualByRtti(std::string_view moduleBasename, std::string_view rttiName,
                     std::size_t vtableSlotOffset);

} // namespace itemphysics
