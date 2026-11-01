// © Joseph Cameron - MIT License

#include <jfc/net/ext/impaired_host.h>
#include <jfc/net/loopback.h>

int main() {
    jfc::net::loopback_server server;

    return server.connect() ? 0 : 1;
}
