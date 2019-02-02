#pragma once

#include "common/common/logger.h"
#include "common/common/enum_to_int.h"
#include "extensions/frozy/upstream/state.h"

namespace Envoy {
namespace Upstream {
namespace Frozy {

class UpstreamConnection : public ConnectionWrap,
                           public Network::ReadFilter,
                           public Network::ConnectionCallbacks,
                           Logger::Loggable<Logger::Id::filter> {
public:
  explicit UpstreamConnection(Network::ConnectionPtr&& connection, UpstreamControlSharedPtr control)
      : ConnectionWrap(std::move(connection)), control_(std::move(control)),
        role_(Role::Handshake) {
    connection_->addConnectionCallbacks(*this);
  }

  // Network::ReadFilter

  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
    if (data.length() == 0 && end_stream) {
      connection_->close(Network::ConnectionCloseType::NoFlush);
      return Network::FilterStatus::Continue;
    }

    switch (role_) {
    case Role::Handshake:
      if (data.length() >= sizeof(Hello)) {
        auto hello = drainHello(data);
        if (hello == CONTROL_HELLO) {
          role_ = Role::Control;
          control_->registerUpstreamControl(shared_from_this());
        } else {
          if (!control_->registerUpstreamService(shared_from_this(), hello)) {
            connection_->close(Network::ConnectionCloseType::NoFlush);
          } else {
            role_ = Role::Service;
          }
        }
      }
      break;
    case Role::Control:
      break;
    case Role::Service:
      return Network::FilterStatus::Continue;
    }

    return Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override {}

  // Network::ConnectionCallbacks

  void onEvent(Network::ConnectionEvent event) override {
    ENVOY_CONN_LOG(trace, "event={}, role {}", *connection_, enumToInt(event), enumToInt(role_));
    if (event == Network::ConnectionEvent::RemoteClose ||
        event == Network::ConnectionEvent::LocalClose) {
      disconnected_ = true;
      control_->removeUpstreamConnection(cid_);
    }
  }

  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  enum class Role {
    Handshake,
    Control,
    Service,
  };

  UpstreamControlSharedPtr control_;
  Role role_;
};

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy
