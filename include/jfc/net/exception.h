// © Joseph Cameron - MIT License

#ifndef JFC_NET_EXCEPTION_H
#define JFC_NET_EXCEPTION_H

#include <exception>
#include <memory>
#include <string>

namespace jfc::net {
    //! what jfc-net throws when networking or a message fails
    class exception final : public std::exception {
        std::shared_ptr<const std::string> m_pWhat;

    public:
        explicit exception(const std::string &aWhat)
        : m_pWhat(std::make_shared<const std::string>(aWhat))
        {}

        exception(const exception &aOther) noexcept = default;

        exception &operator=(const exception &aOther) noexcept = default;

        const char *what() const noexcept override { return m_pWhat->c_str(); }
    };
}

#endif
