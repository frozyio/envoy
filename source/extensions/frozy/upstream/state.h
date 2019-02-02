#pragma once

#include "extensions/frozy/upstream/connection_wrap.h"

#include "envoy/network/connection.h"
#include "envoy/server/filter_config.h"
#include "common/common/thread.h"
#include "common/common/lock_guard.h"
#include "common/common/logger.h"
#include "common/common/assert.h"
#include "extensions/frozy/common/control.h"

namespace Envoy {
namespace Upstream {
namespace Frozy {

#define TRACE_BYTES_COUNT 0

class UpstreamControlCallbacks {
public:
  virtual ~UpstreamControlCallbacks() {}
  virtual void onControlConnected(uint64_t id) PURE;
  virtual void onControlClosure(uint64_t id) PURE;
};

class UpstreamControl : Logger::Loggable<Logger::Id::filter> {
public:
  UpstreamControl(UpstreamControlCallbacks& listener) : listener_(listener) {}

  void newConnection(ConnectionWrapSharedPtr conn);

  void removeDownstreamConnection(uint64_t id);
  void removeUpstreamConnection(uint64_t id);

  void registerUpstreamControl(ConnectionWrapSharedPtr conn);
  bool registerUpstreamService(ConnectionWrapSharedPtr conn, uint64_t downstream_id);
  Event::TimerPtr registerDownstream(ConnectionWrapSharedPtr conn, uint64_t control_conn_id);

private:
  Thread::MutexBasicLockable mutex_;
  UpstreamControlCallbacks& listener_;
  std::map<uint64_t, ConnectionWrapSharedPtr> connecting_downstreams_;
  std::map<uint64_t, ConnectionWrapSharedPtr> controls_;
  std::map<uint64_t, ConnectionWrapSharedPtr> connections_;

  ConnectionWrapSharedPtr lookupDownstream(uint64_t id);
  ConnectionWrapSharedPtr lookupControl(uint64_t id);
  bool requestUpstream(ConnectionWrapSharedPtr downstream, uint64_t control_conn_id);
  bool isProxyEstablished(uint64_t downstream_id);
};

typedef std::shared_ptr<UpstreamControl> UpstreamControlSharedPtr;

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy
