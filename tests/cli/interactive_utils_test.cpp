#include "interactive_utils.h"

#include <gtest/gtest.h>

#include <string>

namespace suzume::cli {
namespace {

TEST(InteractiveUtilsTest, TrimTreatsNonAsciiBytesAsNonWhitespace) {
  const std::string input = std::string(" \t") + static_cast<char>(0xFF) + "x\n";

  EXPECT_EQ(trim(input), std::string(1, static_cast<char>(0xFF)) + "x");
}

}  // namespace
}  // namespace suzume::cli
