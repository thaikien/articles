
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "../r_pow.hpp"

TEST_CASE("r_pow") {

  SECTION("double")
  {
    double base = 14.23;
    REQUIRE (std::pow(base, 0) == r_pow<0>(base));
    REQUIRE (std::pow(base, 1) == r_pow<1>(base));
    REQUIRE (std::pow(base, 2) == r_pow<2>(base));
    REQUIRE (std::pow(base, 13) == Approx(r_pow<13>(base)).epsilon(1e-15));
  }
}
