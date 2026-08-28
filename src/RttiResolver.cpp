#include "RttiResolver.hpp"

#include <algorithm>
#include <cstring>
#include <link.h>
#include <string>

namespace itemphysics {
namespace {

struct ModuleSearchContext {
  std::string basename;
  std::optional<ModuleView> result;
};

std::string_view basenameOf(std::string_view path) {
  const auto pos = path.find_last_of('/');
  return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

int moduleCallback(dl_phdr_info *info, std::size_t, void *opaque) {
  auto &ctx = *static_cast<ModuleSearchContext *>(opaque);
  const std::string_view name = info->dlpi_name ? info->dlpi_name : "";
  if (basenameOf(name) != ctx.basename) {
    return 0;
  }

  ModuleView view;
  view.base = static_cast<std::uintptr_t>(info->dlpi_addr);
  view.ranges.reserve(info->dlpi_phnum);
  for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
    const auto &ph = info->dlpi_phdr[i];
    if (ph.p_type != PT_LOAD || ph.p_memsz == 0) {
      continue;
    }
    const auto begin = view.base + static_cast<std::uintptr_t>(ph.p_vaddr);
    const auto end = begin + static_cast<std::uintptr_t>(ph.p_memsz);
    view.ranges.push_back(MemoryRange{
        .begin = begin,
        .end = end,
        .readable = (ph.p_flags & PF_R) != 0,
        .writable = (ph.p_flags & PF_W) != 0,
        .executable = (ph.p_flags & PF_X) != 0,
    });
  }
  ctx.result = std::move(view);
  return 1;
}

std::optional<std::uintptr_t> findCString(const ModuleView &module,
                                          std::string_view text) {
  std::string needle(text);
  needle.push_back('\0');
  for (const auto &range : module.ranges) {
    if (!range.readable || range.writable || range.end <= range.begin ||
        range.end - range.begin < needle.size()) {
      continue;
    }
    const auto *first = reinterpret_cast<const unsigned char *>(range.begin);
    const auto *last = reinterpret_cast<const unsigned char *>(range.end);
    const auto *it = std::search(first, last, needle.begin(), needle.end());
    if (it != last) {
      return reinterpret_cast<std::uintptr_t>(it);
    }
  }
  return std::nullopt;
}

std::vector<std::uintptr_t> findPointerRefs(const ModuleView &module,
                                            std::uintptr_t value) {
  std::vector<std::uintptr_t> refs;
  for (const auto &range : module.ranges) {
    if (!range.readable || range.end <= range.begin ||
        range.end - range.begin < sizeof(std::uintptr_t)) {
      continue;
    }
    auto address = (range.begin + alignof(std::uintptr_t) - 1) &
                   ~(static_cast<std::uintptr_t>(alignof(std::uintptr_t) - 1));
    for (; address + sizeof(std::uintptr_t) <= range.end;
         address += sizeof(std::uintptr_t)) {
      if (*reinterpret_cast<const std::uintptr_t *>(address) == value) {
        refs.push_back(address);
      }
    }
  }
  return refs;
}

} // namespace

bool ModuleView::readable(std::uintptr_t address, std::size_t size) const noexcept {
  return std::any_of(ranges.begin(), ranges.end(), [&](const MemoryRange &range) {
    return range.readable && range.contains(address, size);
  });
}

bool ModuleView::executable(std::uintptr_t address) const noexcept {
  return std::any_of(ranges.begin(), ranges.end(), [&](const MemoryRange &range) {
    return range.executable && range.contains(address);
  });
}

std::optional<ModuleView> findLoadedModule(std::string_view basename) {
  ModuleSearchContext ctx{std::string(basename), std::nullopt};
  dl_iterate_phdr(moduleCallback, &ctx);
  return ctx.result;
}

std::optional<ResolvedVirtual>
resolveVirtualByRtti(std::string_view moduleBasename, std::string_view rttiName,
                     std::size_t vtableSlotOffset) {
  auto module = findLoadedModule(moduleBasename);
  if (!module) {
    return std::nullopt;
  }

  const auto nameAddress = findCString(*module, rttiName);
  if (!nameAddress) {
    return std::nullopt;
  }

  // Itanium ABI: type_info = [type_info vptr, name pointer].
  for (const auto namePointerAddress : findPointerRefs(*module, *nameAddress)) {
    if (namePointerAddress < sizeof(std::uintptr_t)) {
      continue;
    }
    const auto typeInfo = namePointerAddress - sizeof(std::uintptr_t);
    if (!module->readable(typeInfo, sizeof(std::uintptr_t) * 2)) {
      continue;
    }

    // A class vtable header is [offset-to-top, type_info*], then object vptr.
    for (const auto typeInfoPointerAddress : findPointerRefs(*module, typeInfo)) {
      if (typeInfoPointerAddress < sizeof(std::ptrdiff_t) ||
          !module->readable(typeInfoPointerAddress - sizeof(std::ptrdiff_t),
                            sizeof(std::ptrdiff_t) + sizeof(std::uintptr_t))) {
        continue;
      }
      const auto offsetToTop = *reinterpret_cast<const std::ptrdiff_t *>(
          typeInfoPointerAddress - sizeof(std::ptrdiff_t));
      if (offsetToTop != 0) {
        continue;
      }

      const auto vptr = typeInfoPointerAddress + sizeof(std::uintptr_t);
      const auto slotAddress = vptr + vtableSlotOffset;
      if (!module->readable(slotAddress, sizeof(std::uintptr_t))) {
        continue;
      }
      const auto target = *reinterpret_cast<const std::uintptr_t *>(slotAddress);
      if (!target || !module->executable(target)) {
        continue;
      }
      return ResolvedVirtual{std::move(*module), vptr, target};
    }
  }
  return std::nullopt;
}

} // namespace itemphysics
