#define CATCH_CONFIG_MAIN

#include "catch.hpp"

#include "../ringbuffer.h"
#include "../teestream.h"
#include <iostream>

TEST_CASE("readfrom", "[all]") {
    ringbuffer buf(10);
    teestream tee(buf, std::cout);
    tee << "87654321";

    buf.flush();
    std::cout << buf.readfrom(2,2);
    tee << "ba";
    std::cout << buf.readfrom(8,4);
    std::cout << std::endl << buf.str() << std::endl;
}
