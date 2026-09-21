// © Joseph Cameron - All Rights Reserved

#include <jfc/net/writer.h>

#include <bit>
#include <limits>
#include <utility>

namespace jfc::net {
    namespace {
        template<class unsigned_type>
        void _append_little_endian(std::vector<std::byte> &aBytes, unsigned_type aValue) {
            for (std::size_t i = 0; i < sizeof(unsigned_type); ++i) {
                aBytes.push_back(static_cast<std::byte>(aValue & 0xFFu));
                aValue = static_cast<unsigned_type>(aValue >> 8);
            }
        }
    }

    writer &writer::write_u8(const std::uint8_t aValue) {
        mBytes.push_back(static_cast<std::byte>(aValue));
        return *this;
    }

    writer &writer::write_u16(const std::uint16_t aValue) {
        _append_little_endian(mBytes, aValue);
        return *this;
    }

    writer &writer::write_u32(const std::uint32_t aValue) {
        _append_little_endian(mBytes, aValue);
        return *this;
    }

    writer &writer::write_u64(const std::uint64_t aValue) {
        _append_little_endian(mBytes, aValue);
        return *this;
    }

    writer &writer::write_i8(const std::int8_t aValue) {
        return write_u8(static_cast<std::uint8_t>(aValue));
    }

    writer &writer::write_i16(const std::int16_t aValue) {
        return write_u16(static_cast<std::uint16_t>(aValue));
    }

    writer &writer::write_i32(const std::int32_t aValue) {
        return write_u32(static_cast<std::uint32_t>(aValue));
    }

    writer &writer::write_i64(const std::int64_t aValue) {
        return write_u64(static_cast<std::uint64_t>(aValue));
    }

    writer &writer::write_f32(const float aValue) {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        return write_u32(std::bit_cast<std::uint32_t>(aValue));
    }

    writer &writer::write_f64(const double aValue) {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        return write_u64(std::bit_cast<std::uint64_t>(aValue));
    }

    writer &writer::write_bool(const bool aValue) {
        return write_u8(aValue ? 1 : 0);
    }

    writer &writer::write_varint(std::uint64_t aValue) {
        while (aValue >= 0x80) {
            mBytes.push_back(static_cast<std::byte>((aValue & 0x7F) | 0x80));
            aValue >>= 7;
        }
        mBytes.push_back(static_cast<std::byte>(aValue));
        return *this;
    }

    writer &writer::write_varint_signed(const std::int64_t aValue) {
        const auto bits = static_cast<std::uint64_t>(aValue);
        return write_varint((bits << 1) ^ (aValue < 0 ? ~std::uint64_t{0} : 0));
    }

    writer &writer::write_string(const std::string_view aValue) {
        return write_bytes(std::as_bytes(std::span(aValue.data(), aValue.size())));
    }

    writer &writer::write_bytes(const std::span<const std::byte> aValue) {
        write_varint(aValue.size());
        return write_raw(aValue);
    }

    writer &writer::write_raw(const std::span<const std::byte> aValue) {
        mBytes.insert(mBytes.end(), aValue.begin(), aValue.end());
        return *this;
    }

    std::span<const std::byte> writer::bytes() const {
        return mBytes;
    }

    std::size_t writer::size() const {
        return mBytes.size();
    }

    std::vector<std::byte> writer::take() {
        return std::exchange(mBytes, {});
    }
}
