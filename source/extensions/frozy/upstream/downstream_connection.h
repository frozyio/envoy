#pragma once

#include "common/common/logger.h"
#include "common/common/enum_to_int.h"
#include "extensions/frozy/upstream/state.h"

namespace Envoy {
namespace Upstream {
namespace Frozy {

class DownstreamConnection : public ConnectionWrap,
                             public Network::ConnectionCallbacks,
                             Logger::Loggable<Logger::Id::filter> {
public:
  DownstreamConnection(Network::ConnectionPtr&& connection, UpstreamControlSharedPtr control)
      : ConnectionWrap(std::move(connection)), control_(std::move(control)), conn_timer_(nullptr) {
    connection_->readDisable(true);
    connection_->addConnectionCallbacks(*this);
  }

  void setTimer(Event::TimerPtr&& timer) { conn_timer_ = std::move(timer); }

  void onEvent(Network::ConnectionEvent event) override {
    ENVOY_CONN_LOG(trace, "event {}", *connection_, enumToInt(event));
    if (event == Network::ConnectionEvent::RemoteClose ||
        event == Network::ConnectionEvent::LocalClose) {
      disconnected_ = true;
      control_->removeDownstreamConnection(cid_);
      if (conn_timer_ != nullptr) {
        conn_timer_->disableTimer();
      }
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  UpstreamControlSharedPtr control_;
  Event::TimerPtr conn_timer_;
};

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy
