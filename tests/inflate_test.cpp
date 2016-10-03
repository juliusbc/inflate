#define CATCH_CONFIG_MAIN
#include "catch.hpp"

//#define DEBUG_INFGEN_OUTPUT
//#define DEBUG_INFGEN_OUTPUT_D
//#define DEBUG_DUMP_CODES
#include "../inflate.cpp"
#include "../ifbstream.cpp"
#include "../ringbuffer.cpp"
#include "../huffmantree.cpp"

TEST_CASE("range operations", "[rangeops][utils][all]") {
    
    SECTION("basic range operations") {
        std::vector<inflate::Range> ranges1 = {
            (inflate::Range){1, 4},
            (inflate::Range){4, 6},
            (inflate::Range){6, 4},
            (inflate::Range){14, 5},
            (inflate::Range){18, 6},
            (inflate::Range){21, 4},
            (inflate::Range){26, 6}
        };
        std::vector<inflate::Range> ranges2 = {
            (inflate::Range){0, 8},
            (inflate::Range){4, 10},
            (inflate::Range){5, 8},
            (inflate::Range){7, 7},
            (inflate::Range){8, 5},
            (inflate::Range){13, 5},
            (inflate::Range){16, 4},
            (inflate::Range){17, 5},
            (inflate::Range){21, 4},
            (inflate::Range){22, 3},
            (inflate::Range){24, 4},
            (inflate::Range){26, 5},
            (inflate::Range){27, 8}
        };
        
        SECTION("counting") {
            std::vector<int> temp1 = {0, 0, 0, 0, 7, 8, 12};
            REQUIRE(inflate::_UTIL::count_by_bitlength(ranges1) == temp1);

            std::vector<int> temp2 = {0, 0, 0, 1, 9, 9, 0, 2, 3, 0, 4};
            REQUIRE(inflate::_UTIL::count_by_bitlength(ranges2) == temp2);
        }

        SECTION("grouping") {
            std::vector<unsigned int> lengths = {
                4,4,            // 1
                6,6,6,          // 4
                4,4,            // 6
                5,5,5,5,5,5,5,5,// 14
                6,6,6,6,        // 18
                4,4,4,          // 21
                6,6,6,6,6};     // 26
            auto tranges1 = inflate::_UTIL::group_into_ranges(
                    lengths.begin(), lengths.end());
            REQUIRE(tranges1.size() == ranges1.size());
            REQUIRE(std::equal(tranges1.begin(), tranges1.end(), 
                        ranges1.begin(),
                        [](inflate::Range x, inflate::Range y) {
                            return x.end == y.end 
                            && x.bit_length == y.bit_length;
                            })
                   );
        }
    }
    
    SECTION("more ranges operations") {
        SECTION("1") {
        std::vector<unsigned int> lengths = {4, 0, 0, 6, 5, 3, 3, 3, 4, 4, 3, 3, 4, 0, 0, 0, 6, 5, 5};
        auto tranges = inflate::_UTIL::group_into_ranges(
                lengths.begin(), lengths.end());

        std::vector<inflate::Range> ranges = {
            (inflate::Range){0, 4},
            (inflate::Range){2, 0},
            (inflate::Range){3, 6},
            (inflate::Range){4, 5},
            (inflate::Range){7, 3},
            (inflate::Range){9, 4},
            (inflate::Range){11, 3},
            (inflate::Range){12, 4},
            (inflate::Range){15, 0},
            (inflate::Range){16, 6},
            (inflate::Range){18, 5},
        };
        std::ostringstream out;
        for(auto& range : tranges) {
            out << range.end << ':' << range.bit_length << ", ";
        }
        INFO("tranges = [" << out.str() << ']');
        REQUIRE(tranges.size() == ranges.size());
        REQUIRE(std::equal(tranges.begin(), tranges.end(),
                    ranges.begin(),
                    [](inflate::Range x, inflate::Range y) {
                        return x.end == y.end 
                        && x.bit_length == y.bit_length;
                        })
            );
        }
    }
}

TEST_CASE("build_tree", "[build_tree][utils][all]") {
    std::vector<inflate::Range> ranges1 = {
        (inflate::Range){1, 4},
        (inflate::Range){4, 6},
        (inflate::Range){6, 4},
        (inflate::Range){14, 5},
        (inflate::Range){18, 6},
        (inflate::Range){21, 4},
        (inflate::Range){26, 6}
    };
    std::vector<inflate::Range> ranges2 = {
        (inflate::Range){0, 8},
        (inflate::Range){4, 10},
        (inflate::Range){5, 8},
        (inflate::Range){7, 7},
        (inflate::Range){8, 5},
        (inflate::Range){13, 5},
        (inflate::Range){16, 4},
        (inflate::Range){17, 5},
        (inflate::Range){21, 4},
        (inflate::Range){22, 3},
        (inflate::Range){24, 4},
        (inflate::Range){26, 5},
        (inflate::Range){27, 8}
    };

    
    SECTION("building trees") {
        std::string temp1 = "-1 -1 -1 # -1 -1 -1 26 # # 25 # # -1 24 # # 23 # # -1 -1 22 # # 18 # # -1 17 # # 16 # # -1 -1 -1 -1 15 # # 4 # # -1 3 # # 2 # # -1 14 # # 13 # # -1 -1 12 # # 11 # # -1 10 # # 9 # # -1 -1 -1 -1 8 # # 7 # # 21 # # -1 20 # # 19 # # -1 -1 6 # # 5 # # -1 1 # # 0 # # ";
        REQUIRE(inflate::build_decoder(ranges1).str() == temp1);
        
        std::string temp2 = "-1 -1 -1 -1 -1 -1 -1 -1 -1 -1 4 # # 3 # # -1 2 # # 1 # # 27 # # -1 5 # # 0 # # -1 7 # # 6 # # 26 # # -1 25 # # 17 # # -1 -1 13 # # 12 # # -1 11 # # 10 # # -1 -1 -1 9 # # 8 # # 24 # # -1 23 # # 21 # # -1 -1 -1 20 # # 19 # # -1 18 # # 16 # # -1 -1 15 # # 14 # # 22 # # ";
        REQUIRE(inflate::build_decoder(ranges2).str() == temp2);
    }

}

TEST_CASE("Test header decoding", "[headers][all]") {
    ifbstream testdata("gunzip.c.gz.body");

    SECTION("preheaders") {
        testdata.read(13);

        auto ranges2 = inflate::_UTIL::read_preheader(testdata);
        std::vector<std::vector<int>> temp3 = {{0,3}, {3,0}, {5,4}, {6,3}, {7,2}, {9,3}, {10,4}, {11,5}, {15,0}, {16,6}, {18,7}};
        REQUIRE(std::equal(ranges2.begin(), ranges2.end(), temp3.begin(),
                    [](inflate::Range x, std::vector<int> y) {
                        return x.end == y[0] && x.bit_length == y[1];
                    })
               );
    }

    SECTION("full headers") {
        testdata.read(3);
        
        auto trees = inflate::read_deflate_header(testdata);
        std::string temp4 = "-1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 256 # # 92 # # -1 90 # # 80 # # -1 279 # # 277 # # -1 -1 276 # # 124 # # -1 123 # # 122 # # -1 -1 -1 88 # # 76 # # -1 72 # # 66 # # -1 -1 63 # # 35 # # 120 # # -1 -1 -1 107 # # 93 # # -1 91 # # 85 # # -1 -1 84 # # 83 # # -1 82 # # 79 # # -1 -1 -1 -1 78 # # 77 # # -1 73 # # 70 # # -1 -1 68 # # 67 # # -1 65 # # 58 # # -1 -1 -1 47 # # 39 # # -1 37 # # 33 # # -1 275 # # 274 # # -1 -1 -1 -1 273 # # 125 # # -1 121 # # 119 # # -1 -1 118 # # 106 # # -1 103 # # 69 # # -1 -1 -1 62 # # 60 # # -1 57 # # 55 # # -1 -1 52 # # 43 # # -1 42 # # 41 # # -1 -1 -1 -1 -1 38 # # 34 # # 272 # # -1 271 # # 270 # # -1 -1 117 # # 112 # # -1 109 # # 104 # # -1 -1 -1 102 # # 98 # # -1 95 # # 61 # # -1 -1 59 # # 56 # # -1 54 # # 53 # # -1 -1 -1 -1 -1 51 # # 50 # # -1 48 # # 46 # # -1 -1 45 # # 44 # # -1 40 # # 10 # # -1 -1 269 # # 268 # # -1 264 # # 116 # # -1 -1 -1 115 # # 114 # # -1 111 # # 110 # # -1 -1 108 # # 105 # # -1 100 # # 99 # # -1 -1 -1 -1 -1 97 # # 49 # # 267 # # -1 266 # # 265 # # -1 -1 263 # # 262 # # -1 261 # # 260 # # -1 -1 -1 101 # # 32 # # 259 # # -1 258 # # 257 # # "; 
        std::string temp5 = "-1 -1 -1 -1 -1 -1 -1 -1 -1 4 # # 3 # # -1 2 # # 1 # # -1 5 # # 0 # # -1 7 # # 6 # # -1 27 # # 9 # # -1 26 # # 25 # # -1 -1 17 # # 15 # # -1 13 # # 8 # # -1 -1 24 # # 23 # # -1 22 # # 21 # # -1 -1 -1 20 # # 19 # # -1 18 # # 16 # # -1 -1 14 # # 12 # # -1 11 # # 10 # # ";
        REQUIRE(trees.first.str() == temp4);
        REQUIRE(trees.second.str() == temp5);
    }

    SECTION("gunzipping blocks") {
        testdata.read(3);
        CHECK_NOTHROW(inflate::inflate_block(testdata));
    }
}

TEST_CASE("small", "[fullfiles][all]") {
    CHECK_NOTHROW(inflate::gunzip("inflate_test_copy.cpp.gz"));
}

TEST_CASE("bufferoverflow", "[fullfiles][all]") {
    CHECK_NOTHROW(inflate::gunzip("teestream.h.gch.gz"));
}

