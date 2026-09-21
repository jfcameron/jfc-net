// © Joseph Cameron - All Rights Reserved

#ifndef JFC_NET_WRITER_H
#define JFC_NET_WRITER_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace jfc::net {
    /// \brief builds a message's bytes, one field after another
    ///
    /// Not thread safe; one writer a thread.
    class writer final {
    public:
        /// \name Fixed width
        /// @{
        writer &write_u8(const std::uint8_t aValue);
        writer &write_u16(const std::uint16_t aValue);
        writer &write_u32(const std::uint32_t aValue);
        writer &write_u64(const std::uint64_t aValue);
        writer &write_i8(const std::int8_t aValue);
        writer &write_i16(const std::int16_t aValue);
        writer &write_i32(const std::int32_t aValue);
        writer &write_i64(const std::int64_t aValue);
        writer &write_f32(const float aValue);
        writer &write_f64(const double aValue);
        writer &write_bool(const bool aValue);
        /// @}

        /// \name Variable width: one byte below 128, up to ten for the largest
        /// @{
        writer &write_varint(const std::uint64_t aValue);
        writer &write_varint_signed(const std::int64_t aValue);
        /// @}

        /// \name Length prefixed
        /// @{
        writer &write_string(const std::string_view aValue);
        writer &write_bytes(const std::span<const std::byte> aValue);
        /// @}

        /// \brief bytes appended as they are, with no length: for what the reader knows the size of
        writer &write_raw(const std::span<const std::byte> aValue);

        /// \brief what has been written so far; valid until the next write
        [[nodiscard]] std::span<const std::byte> bytes() const;

        [[nodiscard]] std::size_t size() const;

        /// \brief the bytes, moved out; the writer is empty after
        [[nodiscard]] std::vector<std::byte> take();

    private:
        std::vector<std::byte> mBytes;
    };
}

#endif
