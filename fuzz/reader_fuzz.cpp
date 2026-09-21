// © Joseph Cameron - All Rights Reserved

#include <jfc/net/exception.h>
#include <jfc/net/reader.h>
#include <jfc/net/writer.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace {
    constexpr std::size_t MAX_LENGTH = 64;

    void _write_back(jfc::net::reader &aReader, const std::uint8_t aOperation, jfc::net::writer &aWriter) {
        switch (aOperation % 16) {
            case 0: aWriter.write_u8(aReader.read_u8()); break;
            case 1: aWriter.write_u16(aReader.read_u16()); break;
            case 2: aWriter.write_u32(aReader.read_u32()); break;
            case 3: aWriter.write_u64(aReader.read_u64()); break;
            case 4: aWriter.write_i8(aReader.read_i8()); break;
            case 5: aWriter.write_i16(aReader.read_i16()); break;
            case 6: aWriter.write_i32(aReader.read_i32()); break;
            case 7: aWriter.write_i64(aReader.read_i64()); break;
            case 8: aWriter.write_f32(aReader.read_f32()); break;
            case 9: aWriter.write_f64(aReader.read_f64()); break;
            case 10: aWriter.write_bool(aReader.read_bool()); break;
            case 11: aWriter.write_varint(aReader.read_varint()); break;
            case 12: aWriter.write_varint_signed(aReader.read_varint_signed()); break;
            case 13: aWriter.write_string(aReader.read_string(MAX_LENGTH)); break;
            case 14: aWriter.write_bytes(aReader.read_bytes(MAX_LENGTH)); break;
            case 15: aWriter.write_raw(aReader.read_raw(aOperation / 16)); break;
        }
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *aData, const std::size_t aSize) {
    const auto input = std::as_bytes(std::span(aData, aSize));

    jfc::net::reader reader(input);

    try {
        while (reader.remaining()) {
            const auto operation = reader.read_u8();
            const auto start = input.size() - reader.remaining();

            jfc::net::writer writer;
            _write_back(reader, operation, writer);

            const auto read = input.subspan(start, input.size() - reader.remaining() - start);
            if (!std::ranges::equal(read, writer.bytes())) std::abort();
        }
        reader.expect_end();
    }
    catch (const jfc::net::exception &) {}

    return 0;
}
