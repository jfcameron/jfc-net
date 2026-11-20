// © Joseph Cameron - MIT License

#include <jfc/net/ext/impaired_host.h>
#include <jfc/net/ext/merged_host.h>
#include <jfc/net/loopback.h>

int main() {
    auto pServer = std::make_shared<jfc::net::loopback_server>();
    jfc::net::ext::merged_host merged({pServer});

    return pServer->connect() && merged.poll() ? 0 : 1;
}
