#pragma once

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/enum_to_int.h"
#include "extensions/frozy/upstream/connection_wrap.h"

namespace Envoy {
namespace Upstream {
namespace Frozy {

class ForwardFilter : public Network::ReadFilter,
                      public Network::ConnectionCallbacks,
                      Logger::Loggable<Logger::Id::filter> {
public:
  explicit ForwardFilter(ConnectionWrapSharedPtr connection, ConnectionWrapSharedPtr target)
      : connection_(std::move(connection)), target_(std::move(target)) {
    connection_->connection().addConnectionCallbacks(*this);
  }

  // Network::ReadFilter

  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    target_->writeOnDispatcher(data, end_stream);
    ASSERT(data.length() == 0);
    return Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override {}

  // Network::ConnectionCallbacks

  void onEvent(Network::ConnectionEvent event) override {
    ENVOY_CONN_LOG(trace, "event={}", connection_->connection(), enumToInt(event));
    if (event == Network::ConnectionEvent::RemoteClose ||
        event == Network::ConnectionEvent::LocalClose) {
      target_->closeOnDispatcher(Network::ConnectionCloseType::NoFlush);
    }
  }

  void onAboveWriteBufferHighWatermark() override { target_->readDisableOnDispatcher(true); }

  void onBelowWriteBufferLowWatermark() override { target_->readDisableOnDispatcher(false); }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  ConnectionWrapSharedPtr connection_;
  ConnectionWrapSharedPtr target_;
};

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy
