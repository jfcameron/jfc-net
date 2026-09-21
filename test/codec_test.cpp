// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/net/exception.h>
#include <jfc/net/reader.h>
#include <jfc/net/writer.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace jfc::net;

namespace {
    [[nodiscard]] std::vector<std::byte> bytes_of(const std::initializer_list<int> aValues) {
        std::vector<std::byte> out;
        for (const auto value : aValues) out.push_back(static_cast<std::byte>(value));
        return out;
    }

    [[nodiscard]] std::vector<std::byte> written(const writer &aWriter) {
        return {aWriter.bytes().begin(), aWriter.bytes().end()};
    }
}

TEST_CASE("the writer's layout", "[net][codec]") {
    writer w;

    SECTION("integers are little endian at their width") {
        w.write_u8(0x12).write_u16(0x3456).write_u32(0x789ABCDE).write_u64(0x0102030405060708);

        REQUIRE(written(w) == bytes_of({0x12, 0x56, 0x34, 0xDE, 0xBC, 0x9A, 0x78,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
    }

    SECTION("signed integers are their two's complement bits") {
        w.write_i8(-1).write_i16(-2).write_i32(-3).write_i64(std::numeric_limits<std::int64_t>::min());

        REQUIRE(written(w) == bytes_of({0xFF, 0xFE, 0xFF, 0xFD, 0xFF, 0xFF, 0xFF,
            0, 0, 0, 0, 0, 0, 0, 0x80}));
    }

    SECTION("floats are their IEEE 754 bits") {
        w.write_f32(1.0f).write_f64(-2.0);

        REQUIRE(written(w) == bytes_of({0x00, 0x00, 0x80, 0x3F, 0, 0, 0, 0, 0, 0, 0x00, 0xC0}));
    }

    SECTION("a bool is a byte, 0 or 1") {
        w.write_bool(false).write_bool(true);

        REQUIRE(written(w) == bytes_of({0, 1}));
    }

    SECTION("a varint is seven bits a byte, low first") {
        w.write_varint(0).write_varint(127).write_varint(128).write_varint(300);

        REQUIRE(written(w) == bytes_of({0x00, 0x7F, 0x80, 0x01, 0xAC, 0x02}));
    }

    SECTION("the largest varint is ten bytes") {
        w.write_varint(std::numeric_limits<std::uint64_t>::max());

        REQUIRE(written(w) == bytes_of({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}));
    }

    SECTION("a signed varint zigzags, so small negatives stay small") {
        w.write_varint_signed(0).write_varint_signed(-1).write_varint_signed(1).write_varint_signed(-64)
            .write_varint_signed(64);

        REQUIRE(written(w) == bytes_of({0x00, 0x01, 0x02, 0x7F, 0x80, 0x01}));
    }

    SECTION("strings and byte runs are a varint length, then the bytes") {
        w.write_string("hi").write_bytes(bytes_of({7, 8, 9})).write_string("");

        REQUIRE(written(w) == bytes_of({2, 'h', 'i', 3, 7, 8, 9, 0}));
    }

    SECTION("raw bytes have no length") {
        w.write_raw(bytes_of({4, 5}));

        REQUIRE(written(w) == bytes_of({4, 5}));
    }

    SECTION("take moves the bytes out, and leaves it empty") {
        w.write_u16(1);

        REQUIRE(w.take() == bytes_of({1, 0}));
        REQUIRE(w.size() == 0);
    }
}

TEST_CASE("everything written reads back", "[net][codec]") {
    writer w;
    w.write_u8(200)
        .write_u16(65535)
        .write_u32(4000000000u)
        .write_u64(std::numeric_limits<std::uint64_t>::max())
        .write_i8(-128)
        .write_i16(-30000)
        .write_i32(std::numeric_limits<std::int32_t>::min())
        .write_i64(-5)
        .write_f32(0.1f)
        .write_f64(-1e300)
        .write_bool(true)
        .write_varint(1234567890123)
        .write_varint_signed(std::numeric_limits<std::int64_t>::min())
        .write_varint_signed(std::numeric_limits<std::int64_t>::max())
        .write_string("a chunk's name")
        .write_bytes(bytes_of({0, 255}))
        .write_raw(bytes_of({42}));

    reader r(w.bytes());

    REQUIRE(r.read_u8() == 200);
    REQUIRE(r.read_u16() == 65535);
    REQUIRE(r.read_u32() == 4000000000u);
    REQUIRE(r.read_u64() == std::numeric_limits<std::uint64_t>::max());
    REQUIRE(r.read_i8() == -128);
    REQUIRE(r.read_i16() == -30000);
    REQUIRE(r.read_i32() == std::numeric_limits<std::int32_t>::min());
    REQUIRE(r.read_i64() == -5);
    REQUIRE(r.read_f32() == 0.1f);
    REQUIRE(r.read_f64() == -1e300);
    REQUIRE(r.read_bool());
    REQUIRE(r.read_varint() == 1234567890123);
    REQUIRE(r.read_varint_signed() == std::numeric_limits<std::int64_t>::min());
    REQUIRE(r.read_varint_signed() == std::numeric_limits<std::int64_t>::max());
    REQUIRE(r.read_string(100) == "a chunk's name");
    REQUIRE(r.read_bytes(2) == bytes_of({0, 255}));
    REQUIRE(r.read_raw(1)[0] == std::byte{42});
    REQUIRE(r.remaining() == 0);
    REQUIRE_NOTHROW(r.expect_end());
}

TEST_CASE("floats keep their bits, NaN and infinity included", "[net][codec]") {
    const auto nan = std::bit_cast<float>(std::uint32_t{0x7FC01234});

    writer w;
    w.write_f32(nan).write_f32(-std::numeric_limits<float>::infinity()).write_f64(-0.0);

    reader r(w.bytes());

    REQUIRE(std::bit_cast<std::uint32_t>(r.read_f32()) == 0x7FC01234);
    REQUIRE(r.read_f32() == -std::numeric_limits<float>::infinity());
    REQUIRE(std::signbit(r.read_f64()));
}

TEST_CASE("varints of every width read back", "[net][codec]") {
    std::mt19937_64 random(7);

    for (int bits = 0; bits <= 64; ++bits) {
        const auto top = bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << bits) - 1;
        const auto value = top & random();
        const auto signedValue = static_cast<std::int64_t>(random()) >> (64 - std::max(bits, 1));

        writer w;
        w.write_varint(top).write_varint(value).write_varint_signed(signedValue);

        reader r(w.bytes());

        REQUIRE(r.read_varint() == top);
        REQUIRE(r.read_varint() == value);
        REQUIRE(r.read_varint_signed() == signedValue);
        REQUIRE_NOTHROW(r.expect_end());
    }
}

TEST_CASE("the reader refuses what it cannot read", "[net][codec]") {
    SECTION("fixed width, cut short") {
        const auto data = bytes_of({1, 2, 3});
        reader r(data);

        REQUIRE_THROWS_AS(r.read_u32(), exception);
    }

    SECTION("nothing at all") {
        reader r({});

        REQUIRE_THROWS_AS(r.read_u8(), exception);
        REQUIRE_THROWS_AS(r.read_varint(), exception);
        REQUIRE_THROWS_AS(r.read_string(10), exception);
    }

    SECTION("a bool that is neither 0 nor 1") {
        const auto data = bytes_of({2});
        reader r(data);

        REQUIRE_THROWS_AS(r.read_bool(), exception);
    }

    SECTION("a varint whose last byte says there is more") {
        const auto data = bytes_of({0x80, 0x80});
        reader r(data);

        REQUIRE_THROWS_AS(r.read_varint(), exception);
    }

    SECTION("a varint longer than its value needs") {
        for (const auto &data : {bytes_of({0x80, 0x00}), bytes_of({0x81, 0x80, 0x00}),
                 bytes_of({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00})}) {
            reader r(data);

            REQUIRE_THROWS_AS(r.read_varint(), exception);
        }
    }

    SECTION("a varint wider than 64 bits") {
        for (const auto &data : {bytes_of({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02}),
                 bytes_of({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F})}) {
            reader r(data);

            REQUIRE_THROWS_AS(r.read_varint(), exception);
        }
    }

    SECTION("a string longer than the caller allows") {
        writer w;
        w.write_string("four");
        reader r(w.bytes());

        REQUIRE_THROWS_AS(r.read_string(3), exception);
    }

    SECTION("a string at exactly the length allowed reads") {
        writer w;
        w.write_string("four");
        reader r(w.bytes());

        REQUIRE(r.read_string(4) == "four");
    }

    SECTION("a length longer than what is left, without allocating it") {
        writer w;
        w.write_varint(std::numeric_limits<std::uint64_t>::max());
        reader r(w.bytes());

        REQUIRE_THROWS_AS(r.read_bytes(std::numeric_limits<std::size_t>::max()), exception);
    }

    SECTION("raw bytes past the end") {
        const auto data = bytes_of({1});
        reader r(data);

        REQUIRE_THROWS_AS(r.read_raw(2), exception);
    }

    SECTION("bytes left over, at expect_end") {
        const auto data = bytes_of({1, 2});
        reader r(data);
        (void)r.read_u8();

        REQUIRE(r.remaining() == 1);
        REQUIRE_THROWS_AS(r.expect_end(), exception);
    }
}

TEST_CASE("a refusal says where", "[net][codec]") {
    const auto data = bytes_of({0, 0, 5});
    reader r(data);
    (void)r.read_u16();

    try {
        (void)r.read_bool();
        FAIL("read a bool of 5");
    }
    catch (const exception &e) {
        REQUIRE(std::string(e.what()).find("at byte 2") != std::string::npos);
    }
}

TEST_CASE("the root exception is a runtime_error", "[net][codec]") {
    reader r({});

    REQUIRE_THROWS_AS(r.read_u8(), std::runtime_error);
}
