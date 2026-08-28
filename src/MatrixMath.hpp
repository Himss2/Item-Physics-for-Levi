#pragma once

#include <cmath>
#include <cstring>

namespace itemphysics {

struct Mat4 {
  float m[16]{};
};
static_assert(sizeof(Mat4) == 64);

inline void postTranslate(Mat4 &matrix, float x, float y, float z) {
  float oldC3[4];
  std::memcpy(oldC3, &matrix.m[12], sizeof(oldC3));
  for (int row = 0; row < 4; ++row) {
    matrix.m[12 + row] = matrix.m[row] * x + matrix.m[4 + row] * y +
                         matrix.m[8 + row] * z + oldC3[row];
  }
}

inline void postRotateX(Mat4 &matrix, float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  float c1[4];
  float c2[4];
  std::memcpy(c1, &matrix.m[4], sizeof(c1));
  std::memcpy(c2, &matrix.m[8], sizeof(c2));
  for (int row = 0; row < 4; ++row) {
    matrix.m[4 + row] = c * c1[row] + s * c2[row];
    matrix.m[8 + row] = -s * c1[row] + c * c2[row];
  }
}

inline void postRotateY(Mat4 &matrix, float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  float c0[4];
  float c2[4];
  std::memcpy(c0, &matrix.m[0], sizeof(c0));
  std::memcpy(c2, &matrix.m[8], sizeof(c2));
  for (int row = 0; row < 4; ++row) {
    matrix.m[row] = c * c0[row] - s * c2[row];
    matrix.m[8 + row] = s * c0[row] + c * c2[row];
  }
}

inline void postRotateZ(Mat4 &matrix, float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  float c0[4];
  float c1[4];
  std::memcpy(c0, &matrix.m[0], sizeof(c0));
  std::memcpy(c1, &matrix.m[4], sizeof(c1));
  for (int row = 0; row < 4; ++row) {
    matrix.m[row] = c * c0[row] + s * c1[row];
    matrix.m[4 + row] = -s * c0[row] + c * c1[row];
  }
}

inline void rotateAround(Mat4 &matrix, float x, float y, float z, float rx,
                         float ry, float rz) {
  postTranslate(matrix, x, y, z);
  postRotateX(matrix, rx);
  postRotateY(matrix, ry);
  postRotateZ(matrix, rz);
  postTranslate(matrix, -x, -y, -z);
}

} // namespace itemphysics
