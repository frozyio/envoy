#pragma once

#include "envoy/network/connection.h"
#include "envoy/upstream/cluster_manager.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Frozy {

class UpstreamCallbacks;

class ControlState : public std::enable_shared_from_this<ControlState>,
                     Logger::Loggable<Logger::Id::upstream> {
public:
  explicit ControlState(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager,
                        std::string upstream_cluster_name, std::string broker_cluster_name)
      : dispatcher_(dispatcher), cluster_manager_(cluster_manager),
        upstream_cluster_name_(upstream_cluster_name), broker_cluster_name_(broker_cluster_name),
        permanent_broker_host_(false) {}

  explicit ControlState(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager,
                        std::string upstream_cluster_name, Upstream::HostConstSharedPtr broker_host)
      : dispatcher_(dispatcher), cluster_manager_(cluster_manager),
        upstream_cluster_name_(upstream_cluster_name),
        broker_cluster_name_(broker_host->cluster().name()), broker_host_(std::move(broker_host)),
        permanent_broker_host_(true) {}

  static void
  initializeControlConnection(Upstream::Cluster& broekr_cluster,
                              const ::envoy::api::v2::Cluster_FrozyConnectorConfig& config,
                              Upstream::ClusterManager& cluster_manager,
                              Event::Dispatcher& dispatcher);
  void connect();
  void terminate();
  void reconnectControlConnection();
  void createUpstreamConnectionOnDispatcher(uint64_t id);
  void removeUpstream(uint64_t id);
  void removeDownstream(uint64_t id);

private:
  struct UpstreamConnecting : public Tcp::ConnectionPool::Callbacks,
                              LinkedObject<UpstreamConnecting> {
    ControlState& parent_;
    uint64_t request_;
    Tcp::ConnectionPool::Cancellable* cancel_handle_;

    UpstreamConnecting(ControlState& parent, uint64_t request)
        : parent_(parent), request_(request) {}

    // Tcp::ConnectionPool::Callbacks
    void onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                       Upstream::HostDescriptionConstSharedPtr host) override;
    void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                     Upstream::HostDescriptionConstSharedPtr host) override;

    Tcp::ConnectionPool::Cancellable* newConnection(Tcp::ConnectionPool::Instance* pool);
  };
  typedef std::unique_ptr<UpstreamConnecting> UpstreamConnectingPtr;

  Event::Dispatcher& dispatcher_;
  Upstream::ClusterManager& cluster_manager_;
  std::string upstream_cluster_name_;
  std::string broker_cluster_name_;
  Upstream::HostConstSharedPtr broker_host_{};
  bool permanent_broker_host_;
  Network::ClientConnectionPtr control_conn_;
  std::map<uint64_t, Network::ClientConnectionPtr> downstreams_;
  std::map<uint64_t, std::unique_ptr<UpstreamCallbacks>> upstreams_;
  Event::TimerPtr reconnect_timer_;
  bool terminate_{false};
  std::list<UpstreamConnectingPtr> upstream_connecting_;

  void createControlConnection();
  Upstream::ThreadLocalCluster* getClsuterForConnect(const std::string& cluster_name);
  Upstream::HostConstSharedPtr chooseHost(const std::string& cluster_name);
  void createUpstreamConnection(uint64_t id);
  Network::ClientConnectionPtr createDownstreamConnection();
  void upstreamConnected(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data, uint64_t request);
};
typedef std::shared_ptr<ControlState> ControlStateSharedPtr;

} // namespace Frozy
} // namespace Extensions
} // namespace Envoy
