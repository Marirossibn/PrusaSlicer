#include <catch2/catch.hpp>
#include "libslic3r/libslic3r.h"

#include "libslic3r/Color.hpp"

using namespace Slic3r;

SCENARIO("Color encoding/decoding cycle", "[Color]") {
    GIVEN("Color") {
        const ColorRGB src_rgb(static_cast<unsigned char>(255), static_cast<unsigned char>(127), static_cast<unsigned char>(63));
        WHEN("apply encode/decode cycle") {
            const std::string encoded = encode_color(src_rgb);
            ColorRGB res_rgb;
            decode_color(encoded, res_rgb);
            const bool ret = res_rgb.r_uchar() == src_rgb.r_uchar() && res_rgb.g_uchar() == src_rgb.g_uchar() && res_rgb.b_uchar() == src_rgb.b_uchar();
            THEN("result matches source") {
                REQUIRE(ret);
            }
        }
    }
}

SCENARIO("Color picking encoding/decoding cycle", "[Color]") {
    GIVEN("Picking color") {
        const ColorRGB src_rgb(static_cast<unsigned char>(255), static_cast<unsigned char>(127), static_cast<unsigned char>(63));
        WHEN("apply encode/decode cycle") {
            const unsigned int encoded = picking_encode(src_rgb.r_uchar(), src_rgb.g_uchar(), src_rgb.b_uchar());
            const ColorRGBA res_rgba = picking_decode(encoded);
            const bool ret = res_rgba.r_uchar() == src_rgb.r_uchar() && res_rgba.g_uchar() == src_rgb.g_uchar() && res_rgba.b_uchar() == src_rgb.b_uchar();
            THEN("result matches source") {
                REQUIRE(ret);
            }
        }
    }
}


