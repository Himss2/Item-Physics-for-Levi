#include "ItemPhysicsConfig.hpp"

#include <cassert>
#include <iostream>

int main() {
  itemphysics::ItemPhysicsConfig defaults{};
  assert(defaults.version == 10);
  assert(!defaults.singleModel);
  assert(!defaults.separateDropVisuals);
  assert(defaults.hideItemShadow);

  itemphysics::ItemPhysicsConfig migrated{
      .version = 1,
      .singleModel = true,
      .separateDropVisuals = true,
      .hideItemShadow = false,
  };
  itemphysics::normalize(migrated);
  assert(migrated.version == 10);
  assert(migrated.singleModel);
  assert(migrated.separateDropVisuals);
  assert(!migrated.hideItemShadow);

  std::cout << "config state boundaries passed\n";
}
