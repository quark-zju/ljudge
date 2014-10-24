#include "sha1.hpp"
#include <cstdio>
#include <cstdint>
#include <string>

extern "C" {
#include "deps/sha1/sha1.h"
#include "deps/sha1/sha1.c"
}

std::string sha1(const std::string& content) {
  SHA1_CTX sha;
  uint8_t results[20];

  SHA1Init(&sha);
  SHA1Update(&sha, (uint8_t *)content.data(), content.length());
  SHA1Final(results, &sha);

  // Convert binary to string
  char result[41];
  for (int n = 0; n < 20; n++) {
    snprintf(result + n * 2, 3, "%02x", results[n]);
  }

  return result;
}
