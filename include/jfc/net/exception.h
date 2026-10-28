// © Joseph Cameron - MIT License

#ifndef JFC_NET_EXCEPTION_H
#define JFC_NET_EXCEPTION_H

#include <stdexcept>

namespace jfc::net {
    //! what jfc-net throws when networking or a message fails
    class exception final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };
}

#endif
