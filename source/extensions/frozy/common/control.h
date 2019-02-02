#pragma once

#include <stdint.h>
#include "common/buffer/buffer_impl.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Upstream {
namespace Frozy {

typedef uint64_t Hello;
static const Hello CONTROL_HELLO = 0;

inline void writeHello(Network::Connection& conn, Hello id) {
  Buffer::OwnedImpl buffer;
  buffer.writeLEInt<uint64_t>(id);
  conn.write(buffer, false);
}

inline Hello drainHello(Buffer::Instance& data) { return data.drainLEInt<uint64_t>(); }

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy