#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/cluster_manager.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

#include "extensions/frozy/connector/control_state.h"
#include "extensions/frozy/connector/downstream.h"

namespace Envoy {
namespace Extensions {
namespace Frozy {

class UpstreamCallbacks : public Tcp::ConnectionPool::UpstreamCallbacks,
                          Logger::Loggable<Logger::Id::filter> {
public:
  UpstreamCallbacks(Tcp::ConnectionPool::ConnectionDataPtr&& upstream,
                    Network::Connection& downstream, ControlStateSharedPtr state)
      : conn_(std::move(upstream)), downstream_(downstream), state_(state) {}

  Network::ClientConnection& connection() { return conn_->connection(); }

  void initializeCallbacks() {
    conn_->connection().enableHalfClose(true);
    conn_->addUpstreamCallbacks(*this);
  }

  void onUpstreamData(Buffer::Instance& data, bool end_stream) override {
    downstream_.write(data, end_stream);
  }

  void onEvent(Network::ConnectionEvent event) override {
    ENVOY_CONN_LOG(trace, "UpstreamCallbacks::onEvent({})", conn_->connection(), enumToInt(event));
    switch (event) {
    case Network::ConnectionEvent::RemoteClose:
    case Network::ConnectionEvent::LocalClose:
      downstream_.close(Network::ConnectionCloseType::FlushWrite);
      state_->removeUpstream(conn_->connection().id());
      break;
    case Network::ConnectionEvent::Connected:
      break;
    }
  }

  void onAboveWriteBufferHighWatermark() override { downstream_.readDisable(true); }
  void onBelowWriteBufferLowWatermark() override { downstream_.readDisable(false); }

private:
  Tcp::ConnectionPool::ConnectionDataPtr conn_;
  Network::Connection& downstream_;
  ControlStateSharedPtr state_;
};

} // namespace Frozy
} // namespace Extensions
} // namespace Envoy
