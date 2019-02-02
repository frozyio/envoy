#include "extensions/frozy/upstream/state.h"
#include "extensions/frozy/upstream/forward_filter.h"

namespace Envoy {
namespace Upstream {
namespace Frozy {

void UpstreamControl::registerUpstreamControl(ConnectionWrapSharedPtr conn) {
  Thread::LockGuard guard(mutex_);
  controls_[conn->id()] = conn;
  ENVOY_CONN_LOG(info, "Frozy upstream control connected to {}", conn->connection(),
                 conn->connection().localAddress()->asString());
  listener_.onControlConnected(conn->id());
}

bool UpstreamControl::requestUpstream(ConnectionWrapSharedPtr downstream,
                                      uint64_t control_conn_id) {
  if (auto control = lookupControl(control_conn_id)) {
    uint64_t id = downstream->id();
    connecting_downstreams_.insert(std::make_pair(id, downstream));
    control->executeOnDispatcher([id](Network::Connection& conn) -> void {
      ENVOY_CONN_LOG(debug, "Requesting upstream for downstream C{} from control C{}", conn, id,
                     conn.id());
      writeHello(conn, id);
    });
    return true;
  }
  return false;
}

Event::TimerPtr UpstreamControl::registerDownstream(ConnectionWrapSharedPtr conn,
                                                    uint64_t control_conn_id) {
  ENVOY_CONN_LOG(trace, "Frozy proxy requesting upstream service", conn->connection());
  if (requestUpstream(conn, control_conn_id)) {
    return conn->connection().dispatcher().createTimer([this, conn]() {
      if (!isProxyEstablished(conn->id()) && conn->connected()) {
        ENVOY_CONN_LOG(error, "Frozy proxy connecting to upstream timeout", conn->connection());
        conn->connection().close(Network::ConnectionCloseType::NoFlush);
      }
    });
  } else {
    ENVOY_CONN_LOG(error, "Frozy proxy failed to request upstream service", conn->connection(),
                   conn->connection().remoteAddress()->asString());
    conn->connection().close(Network::ConnectionCloseType::NoFlush);
  }
  return nullptr;
}

ConnectionWrapSharedPtr UpstreamControl::lookupDownstream(uint64_t id) {
  Thread::LockGuard guard(mutex_);
  auto it = connecting_downstreams_.find(id);
  if (it != connecting_downstreams_.end()) {
    auto downstream = it->second;
    connecting_downstreams_.erase(it);
    return std::move(downstream);
  }
  return nullptr;
}

ConnectionWrapSharedPtr UpstreamControl::lookupControl(uint64_t id) {
  Thread::LockGuard guard(mutex_);
  auto it = controls_.find(id);
  if (it != controls_.end()) {
    return it->second;
  }
  return nullptr;
}

bool UpstreamControl::registerUpstreamService(ConnectionWrapSharedPtr upstream,
                                              uint64_t downstream_id) {
  auto downstream = lookupDownstream(downstream_id);
  if (downstream != nullptr && downstream->connected()) {
    ENVOY_CONN_LOG(debug, "Upstream service established for C{}", upstream->connection(),
                   downstream_id);
    downstream->connection().addReadFilter(std::make_shared<ForwardFilter>(downstream, upstream));
    upstream->connection().addReadFilter(std::make_shared<ForwardFilter>(upstream, downstream));
    downstream->executeOnDispatcher(
        [](Network::Connection& conn) -> void { conn.readDisable(false); });
    return true;
  }

  ENVOY_CONN_LOG(debug, "Downstream C{} not found", upstream->connection(), downstream_id);
  return false;
}

void UpstreamControl::newConnection(ConnectionWrapSharedPtr conn) {
  Thread::LockGuard guard(mutex_);
  connections_.insert(std::make_pair(conn->id(), conn));
}

void UpstreamControl::removeDownstreamConnection(uint64_t id) {
  Thread::LockGuard guard(mutex_);
  connections_.erase(id);
  connecting_downstreams_.erase(id);
}

void UpstreamControl::removeUpstreamConnection(uint64_t id) {
  Thread::LockGuard guard(mutex_);
  connections_.erase(id);
  auto conn = controls_.find(id);
  if (conn != controls_.end()) {
    ENVOY_CONN_LOG(info, "Frozy upstream control disconnected", conn->second->connection());
    listener_.onControlClosure(id);
    controls_.erase(conn);
  }
}

bool UpstreamControl::isProxyEstablished(uint64_t downstream_id) {
  Thread::LockGuard guard(mutex_);
  return connecting_downstreams_.find(downstream_id) == connecting_downstreams_.end();
}

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy
