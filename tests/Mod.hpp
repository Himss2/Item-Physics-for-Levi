#pragma once
#include <string_view>
// Host-only logger boundary. Never included by the Android CMake target.
namespace ll::mod {
struct Logger {
  template <class... T> void info(std::string_view, T &&...) {}
  template <class... T> void warn(std::string_view, T &&...) {}
  template <class... T> void error(std::string_view, T &&...) {}
};
struct NativeMod {
  Logger &getLogger() { static Logger logger; return logger; }
};
}
