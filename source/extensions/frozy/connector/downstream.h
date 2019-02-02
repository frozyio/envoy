#pragma once

#include "envoy/network/filter.h"
#include "envoy/network/connection.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

#include "extensions/frozy/common/control.h"
#include "extensions/frozy/connector/control_state.h"

namespace Envoy {
namespace Extensions {
namespace Frozy {

using namespace Upstream::Frozy;

class DownstreamFilter : public Network::ReadFilter,
                         public Network::ConnectionCallbacks,
                         Logger::Loggable<Logger::Id::filter> {
public:
  DownstreamFilter(Network::Connection& upstream, Hello id, ControlStateSharedPtr state)
      : upstream_(upstream), request_id_(id), state_(state) {}

  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    upstream_.write(data, end_stream);
    return Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    read_callbacks_->connection().addConnectionCallbacks(*this);
    read_callbacks_->connection().enableHalfClose(true);
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override {
    ENVOY_CONN_LOG(trace, "DownstreamFilter::onEvent({})", read_callbacks_->connection(),
                   enumToInt(event));
    switch (event) {
    case Network::ConnectionEvent::RemoteClose:
    case Network::ConnectionEvent::LocalClose:
      upstream_.close(Network::ConnectionCloseType::FlushWrite);
      state_->removeDownstream(read_callbacks_->connection().id());
      break;
    case Network::ConnectionEvent::Connected:
      writeHello(read_callbacks_->connection(), request_id_);
      break;
    }
  }

  void onAboveWriteBufferHighWatermark() override { upstream_.readDisable(true); }
  void onBelowWriteBufferLowWatermark() override { upstream_.readDisable(false); }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::Connection& upstream_;
  Hello request_id_;
  ControlStateSharedPtr state_;
};

} // namespace Frozy
} // namespace Extensions
} // namespace Envoy
