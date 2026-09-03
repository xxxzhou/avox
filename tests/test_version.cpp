#include <avox/version.h>

#include <gtest/gtest.h>

TEST(version, reports_semantic_version) {
  EXPECT_EQ(AVOX_VERSION_MAJOR, 0);
  EXPECT_EQ(AVOX_VERSION_MINOR, 1);
  EXPECT_EQ(AVOX_VERSION_PATCH, 0);
  EXPECT_STREQ(AVOX_VERSION_STRING, "0.1.0");
}
