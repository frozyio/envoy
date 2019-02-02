#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/connection.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

#include "extensions/frozy/common/control.h"
#include "extensions/frozy/connector/upstream.h"
#include "extensions/frozy/connector/control_state.h"

namespace Envoy {
namespace Extensions {
namespace Frozy {

class ControlFilter : public Network::ReadFilter,
                      public Network::ConnectionCallbacks,
                      Logger::Loggable<Logger::Id::filter> {
public:
  ControlFilter(ControlStateSharedPtr&& state) : state_(std::move(state)) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool /* end_stream*/) override {
    while (data.length() >= sizeof(Hello)) {
      auto hello = drainHello(data);
      ENVOY_CONN_LOG(debug, "Frozy control request {}", *conn_, hello);
      state_->createUpstreamConnectionOnDispatcher(hello);
    }
    return Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    conn_ = &callbacks.connection();
    conn_->addConnectionCallbacks(*this);
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    ENVOY_CONN_LOG(trace, "ControlFilter::onEvent({})", *conn_, enumToInt(event));
    if (event == Network::ConnectionEvent::RemoteClose ||
        event == Network::ConnectionEvent::LocalClose) {
      state_->reconnectControlConnection();
    } else {
      ENVOY_CONN_LOG(info, "Frozy control channel connected to {}", *conn_,
                     conn_->remoteAddress()->asString());
      writeHello(*conn_, CONTROL_HELLO);
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  Network::Connection* conn_;
  ControlStateSharedPtr state_;
};

} // namespace Frozy
} // namespace Extensions
} // namespace Envoy
