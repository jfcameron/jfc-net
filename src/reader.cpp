// © Joseph Cameron - All Rights Reserved

#include <jfc/net/reader.h>
#include <jfc/net/exception.h>

#include <bit>
#include <limits>

namespace jfc::net {
    namespace {
        template<class unsigned_type>
        [[nodiscard]] unsigned_type _from_little_endian(const std::span<const std::byte> aBytes) {
            unsigned_type value = 0;
            for (std::size_t i = sizeof(unsigned_type); i-- > 0;) {
                value = static_cast<unsigned_type>((value << 8) | std::to_integer<unsigned_type>(aBytes[i]));
            }
            return value;
        }

        [[nodiscard]] std::string _describe(const std::size_t aPosition) {
            return " at byte " + std::to_string(aPosition);
        }
    }

    reader::reader(const std::span<const std::byte> aBytes)
    : mBytes(aBytes) {}

    std::span<const std::byte> reader::_take(const std::size_t aCount, const char *aWhat) {
        if (aCount > remaining()) {
            throw exception(std::string("message ends in ") + aWhat + _describe(mPosition) + ": wanted "
                + std::to_string(aCount) + " bytes, " + std::to_string(remaining()) + " left");
        }
        const auto out = mBytes.subspan(mPosition, aCount);
        mPosition += aCount;
        return out;
    }

    std::uint8_t reader::read_u8() {
        return std::to_integer<std::uint8_t>(_take(1, "a u8")[0]);
    }

    std::uint16_t reader::read_u16() {
        return _from_little_endian<std::uint16_t>(_take(2, "a u16"));
    }

    std::uint32_t reader::read_u32() {
        return _from_little_endian<std::uint32_t>(_take(4, "a u32"));
    }

    std::uint64_t reader::read_u64() {
        return _from_little_endian<std::uint64_t>(_take(8, "a u64"));
    }

    std::int8_t reader::read_i8() {
        return static_cast<std::int8_t>(read_u8());
    }

    std::int16_t reader::read_i16() {
        return static_cast<std::int16_t>(read_u16());
    }

    std::int32_t reader::read_i32() {
        return static_cast<std::int32_t>(read_u32());
    }

    std::int64_t reader::read_i64() {
        return static_cast<std::int64_t>(read_u64());
    }

    float reader::read_f32() {
        return std::bit_cast<float>(read_u32());
    }

    double reader::read_f64() {
        return std::bit_cast<double>(read_u64());
    }

    bool reader::read_bool() {
        const auto position = mPosition;
        switch (read_u8()) {
            case 0: return false;
            case 1: return true;
            default: throw exception("a bool is neither 0 nor 1" + _describe(position));
        }
    }

    std::uint64_t reader::read_varint() {
        const auto start = mPosition;
        std::uint64_t value = 0;
        for (unsigned shift = 0;; shift += 7) {
            const auto byte = std::to_integer<std::uint64_t>(_take(1, "a varint")[0]);
            const auto bits = byte & 0x7F;

            if (shift == 63 && byte > 1) {
                throw exception("a varint wider than 64 bits" + _describe(start));
            }
            value |= bits << shift;

            if (!(byte & 0x80)) {
                if (bits == 0 && shift != 0) {
                    throw exception("a varint longer than its value needs" + _describe(start));
                }
                return value;
            }
        }
    }

    std::int64_t reader::read_varint_signed() {
        const auto bits = read_varint();
        return static_cast<std::int64_t>((bits >> 1) ^ (~(bits & 1) + 1));
    }

    std::size_t reader::_read_length(const std::size_t aMaxLength, const char *aWhat) {
        const auto position = mPosition;
        const auto length = read_varint();
        if (length > aMaxLength) {
            throw exception(std::string(aWhat) + " of " + std::to_string(length) + " bytes"
                + _describe(position) + ", longer than the " + std::to_string(aMaxLength) + " allowed");
        }
        return static_cast<std::size_t>(length);
    }

    std::string reader::read_string(const std::size_t aMaxLength) {
        const auto bytes = _take(_read_length(aMaxLength, "a string"), "a string");
        return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }

    std::vector<std::byte> reader::read_bytes(const std::size_t aMaxLength) {
        const auto bytes = _take(_read_length(aMaxLength, "a byte run"), "a byte run");
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    std::span<const std::byte> reader::read_raw(const std::size_t aCount) {
        return _take(aCount, "raw bytes");
    }

    std::size_t reader::remaining() const {
        return mBytes.size() - mPosition;
    }

    void reader::expect_end() const {
        if (remaining()) {
            throw exception("message has " + std::to_string(remaining()) + " bytes past its end"
                + _describe(mPosition));
        }
    }
}
