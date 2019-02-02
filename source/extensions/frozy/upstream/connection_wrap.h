#pragma once

#include "common/common/logger.h"
#include "common/common/enum_to_int.h"
#include "common/buffer/buffer_impl.h"
#include "envoy/network/connection.h"
#include "envoy/event/dispatcher.h"

#include <atomic>

namespace Envoy {
namespace Upstream {
namespace Frozy {

class ConnectionWrap;

class ConnectionWrap : public std::enable_shared_from_this<ConnectionWrap> {
public:
  ConnectionWrap(Network::ConnectionPtr&& connection)
      : cid_(connection->id()), connection_(std::move(connection)), disconnected_(false) {
    connection_->enableHalfClose(true);
    connection_->noDelay(true);
  }

  bool connected() { return !disconnected_; }
  Network::Connection& connection() const { return *connection_; }
  uint64_t id() const { return cid_; }

  void executeOnDispatcher(std::function<void(Network::Connection&)> f) {
    if (!disconnected_) {
      connection_->dispatcher().post([this, f]() -> void {
        if (!disconnected_) {
          f(*connection_);
        }
      });
    }
  }

  void closeOnDispatcher(Network::ConnectionCloseType type) {
    executeOnDispatcher([type](Network::Connection& conn) -> void { conn.close(type); });
  }

  void writeOnDispatcher(Buffer::Instance& data, bool end_stream) {
    auto buf = std::make_shared<Buffer::OwnedImpl>();
    buf->move(data);
    executeOnDispatcher(
        [buf, end_stream](Network::Connection& conn) -> void { conn.write(*buf, end_stream); });
  }

  void readDisableOnDispatcher(bool disable) {
    executeOnDispatcher(
        [disable](Network::Connection& conn) -> void { conn.readDisable(disable); });
  }

protected:
  uint64_t cid_;
  Network::ConnectionPtr connection_;
  std::atomic<bool> disconnected_;
};

typedef std::shared_ptr<ConnectionWrap> ConnectionWrapSharedPtr;

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy
