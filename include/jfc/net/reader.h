// © Joseph Cameron - All Rights Reserved

#ifndef JFC_NET_READER_H
#define JFC_NET_READER_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace jfc::net {
    /// \brief reads back what a writer wrote, in the same order, refusing what it cannot read
    class reader final {
    public:
        explicit reader(const std::span<const std::byte> aBytes);

        /// \name Fixed width
        /// @{
        [[nodiscard]] std::uint8_t read_u8();
        [[nodiscard]] std::uint16_t read_u16();
        [[nodiscard]] std::uint32_t read_u32();
        [[nodiscard]] std::uint64_t read_u64();
        [[nodiscard]] std::int8_t read_i8();
        [[nodiscard]] std::int16_t read_i16();
        [[nodiscard]] std::int32_t read_i32();
        [[nodiscard]] std::int64_t read_i64();
        [[nodiscard]] float read_f32();
        [[nodiscard]] double read_f64();
        [[nodiscard]] bool read_bool();
        /// @}

        /// \name Variable width
        /// @{
        [[nodiscard]] std::uint64_t read_varint();
        [[nodiscard]] std::int64_t read_varint_signed();
        /// @}

        /// \name Length prefixed, the length bounded by the caller: there is no unbounded read
        /// @{
        [[nodiscard]] std::string read_string(const std::size_t aMaxLength);
        [[nodiscard]] std::vector<std::byte> read_bytes(const std::size_t aMaxLength);
        /// @}

        /// \brief exactly aCount bytes, with no length before them; a view into the reader's bytes
        [[nodiscard]] std::span<const std::byte> read_raw(const std::size_t aCount);

        /// \brief how many bytes are left unread
        [[nodiscard]] std::size_t remaining() const;

        /// \brief throws unless every byte has been read
        void expect_end() const;

    private:
        [[nodiscard]] std::span<const std::byte> _take(const std::size_t aCount, const char *aWhat);
        [[nodiscard]] std::size_t _read_length(const std::size_t aMaxLength, const char *aWhat);

        std::span<const std::byte> mBytes;
        std::size_t mPosition = 0;
    };
}

#endif
