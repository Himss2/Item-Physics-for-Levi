#pragma once
// Hook installation cannot run on a host without Minecraft/Levi. This double
// deliberately reports failure; tests invoke the production detour boundary.
namespace pl::memory {
enum class HookPriority { Normal };
struct HookHandle {
  HookHandle(void *, void *, void **, HookPriority) {}
  bool installed() const { return false; }
  void reset() {}
};
}
