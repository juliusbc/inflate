#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "../ifbstream.cpp"

TEST_CASE("Reading bits from a file", "[ifbstream]") {
    ifbstream A("ifbstream.txt");

    REQUIRE( A.next() == 1 );
    REQUIRE( A.read(15) == (2 << 7) );
}
