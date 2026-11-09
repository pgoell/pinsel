#include <gtest/gtest.h>

#include "app/project.hpp"

TEST(Project, ExportsItsName) {
  EXPECT_EQ(pinsel::name(), "pinsel");
}
