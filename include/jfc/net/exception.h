// © Joseph Cameron - All Rights Reserved

#ifndef JFC_NET_EXCEPTION_H
#define JFC_NET_EXCEPTION_H

#include <stdexcept>

namespace jfc::net {
    class exception final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };
}

#endif
