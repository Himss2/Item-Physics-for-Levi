#include "RttiResolver.hpp"

#include <cstring>
#include <link.h>

namespace itemphysics {
namespace {

struct ModuleSearchContext {
  std::string_view basename;
  std::optional<ModuleView> result;
};

constexpr std::uintptr_t align4(std::uintptr_t value) noexcept {
  return (value + 3u) & ~std::uintptr_t{3u};
}

void readBuildId(const dl_phdr_info &info, const ElfW(Phdr) &header,
                 ModuleView &module) noexcept {
  struct NoteHeader {
    std::uint32_t nameSize;
    std::uint32_t descriptorSize;
    std::uint32_t type;
  };

  auto cursor = static_cast<std::uintptr_t>(info.dlpi_addr) +
                static_cast<std::uintptr_t>(header.p_vaddr);
  const auto end = cursor + static_cast<std::uintptr_t>(header.p_memsz);
  while (cursor <= end && sizeof(NoteHeader) <= end - cursor) {
    const auto &note = *reinterpret_cast<const NoteHeader *>(cursor);
    cursor += sizeof(NoteHeader);
    if (note.nameSize > end - cursor)
      return;
    const auto *name = reinterpret_cast<const char *>(cursor);
    cursor = align4(cursor + note.nameSize);
    if (cursor > end || note.descriptorSize > end - cursor)
      return;

    if (note.type == NT_GNU_BUILD_ID && note.nameSize >= 3 &&
        std::memcmp(name, "GNU", 3) == 0 &&
        note.descriptorSize == module.buildId.size()) {
      std::memcpy(module.buildId.data(), reinterpret_cast<const void *>(cursor),
                  module.buildId.size());
      module.hasBuildId = true;
      return;
    }
    cursor = align4(cursor + note.descriptorSize);
  }
}

std::string_view basenameOf(std::string_view path) noexcept {
  const auto position = path.find_last_of('/');
  return position == std::string_view::npos ? path : path.substr(position + 1);
}

int moduleCallback(dl_phdr_info *info, std::size_t, void *opaque) {
  auto &context = *static_cast<ModuleSearchContext *>(opaque);
  const std::string_view name = info->dlpi_name ? info->dlpi_name : "";
  if (basenameOf(name) != context.basename)
    return 0;

  ModuleView module;
  module.base = static_cast<std::uintptr_t>(info->dlpi_addr);
  for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
    const auto &header = info->dlpi_phdr[i];
    if (header.p_type == PT_NOTE)
      readBuildId(*info, header, module);
    if (header.p_type != PT_LOAD || header.p_memsz == 0)
      continue;
    if (module.rangeCount == module.ranges.size())
      return 0;

    const auto begin =
        module.base + static_cast<std::uintptr_t>(header.p_vaddr);
    module.ranges[module.rangeCount++] = MemoryRange{
        .begin = begin,
        .end = begin + static_cast<std::uintptr_t>(header.p_memsz),
        .readable = (header.p_flags & PF_R) != 0,
        .writable = (header.p_flags & PF_W) != 0,
        .executable = (header.p_flags & PF_X) != 0,
    };
  }
  context.result = module;
  return 1;
}

std::optional<std::uintptr_t> findCString(const ModuleView &module,
                                          std::string_view text) noexcept {
  for (std::size_t rangeIndex = 0; rangeIndex < module.rangeCount;
       ++rangeIndex) {
    const auto &range = module.ranges[rangeIndex];
    if (!range.readable || range.writable || range.end <= range.begin ||
        range.end - range.begin <= text.size())
      continue;

    for (auto address = range.begin;
         address + text.size() < range.end; ++address) {
      const auto *bytes = reinterpret_cast<const char *>(address);
      if (bytes[text.size()] == '\0' &&
          std::memcmp(bytes, text.data(), text.size()) == 0)
        return address;
    }
  }
  return std::nullopt;
}

template <typename Callback>
bool forEachPointerReference(const ModuleView &module, std::uintptr_t value,
                             Callback callback) noexcept {
  for (std::size_t rangeIndex = 0; rangeIndex < module.rangeCount;
       ++rangeIndex) {
    const auto &range = module.ranges[rangeIndex];
    if (!range.readable || range.end <= range.begin ||
        range.end - range.begin < sizeof(std::uintptr_t))
      continue;

    auto address = (range.begin + alignof(std::uintptr_t) - 1) &
                   ~(static_cast<std::uintptr_t>(alignof(std::uintptr_t) - 1));
    for (; address + sizeof(std::uintptr_t) <= range.end;
         address += sizeof(std::uintptr_t)) {
      if (*reinterpret_cast<const std::uintptr_t *>(address) == value &&
          callback(address))
        return true;
    }
  }
  return false;
}

} // namespace

bool ModuleView::readable(std::uintptr_t address, std::size_t size) const noexcept {
  for (std::size_t i = 0; i < rangeCount; ++i) {
    if (ranges[i].readable && ranges[i].contains(address, size))
      return true;
  }
  return false;
}

bool ModuleView::executable(std::uintptr_t address) const noexcept {
  for (std::size_t i = 0; i < rangeCount; ++i) {
    if (ranges[i].executable && ranges[i].contains(address))
      return true;
  }
  return false;
}

std::optional<ModuleView> findLoadedModule(std::string_view basename) {
  ModuleSearchContext context{basename, std::nullopt};
  dl_iterate_phdr(moduleCallback, &context);
  return context.result;
}

std::optional<ResolvedVirtual>
resolveVirtualByRtti(std::string_view moduleBasename, std::string_view rttiName,
                     std::size_t vtableSlotOffset) {
  const auto module = findLoadedModule(moduleBasename);
  if (!module)
    return std::nullopt;
  const auto nameAddress = findCString(*module, rttiName);
  if (!nameAddress)
    return std::nullopt;

  std::optional<ResolvedVirtual> result;
  forEachPointerReference(*module, *nameAddress, [&](std::uintptr_t nameRef) {
    if (nameRef < sizeof(std::uintptr_t))
      return false;
    const auto typeInfo = nameRef - sizeof(std::uintptr_t);
    if (!module->readable(typeInfo, sizeof(std::uintptr_t) * 2))
      return false;

    return forEachPointerReference(*module, typeInfo,
                                   [&](std::uintptr_t typeInfoRef) {
      if (typeInfoRef < sizeof(std::ptrdiff_t) ||
          !module->readable(typeInfoRef - sizeof(std::ptrdiff_t),
                            sizeof(std::ptrdiff_t) + sizeof(std::uintptr_t)))
        return false;
      const auto offsetToTop = *reinterpret_cast<const std::ptrdiff_t *>(
          typeInfoRef - sizeof(std::ptrdiff_t));
      if (offsetToTop != 0)
        return false;

      const auto vptr = typeInfoRef + sizeof(std::uintptr_t);
      const auto slot = vptr + vtableSlotOffset;
      if (!module->readable(slot, sizeof(std::uintptr_t)))
        return false;
      const auto target = *reinterpret_cast<const std::uintptr_t *>(slot);
      if (!target || !module->executable(target))
        return false;

      result = ResolvedVirtual{*module, vptr, target};
      return true;
    });
  });
  return result;
}

} // namespace itemphysics
