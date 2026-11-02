#include <gtest/gtest.h>

#include "project.hpp"

TEST(Project, ExportsItsName) {
  EXPECT_EQ(pinsel::name(), "pinsel");
}
