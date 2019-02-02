#include "extensions/frozy/connector/control.h"

namespace Envoy {
namespace Extensions {
namespace Frozy {

class ControlManager {
public:
  ControlManager(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager)
      : dispatcher_(dispatcher), cluster_manager_(cluster_manager) {}

  void terminate() {
    dispatcher_.post([this]() -> void {
      for (auto& it : controls_) {
        it.second->terminate();
      }
      controls_.clear();
    });
  }

  void addControlState(const std::string& upstream_cluster_name,
                       const std::string& broker_cluster_name) {
    auto control = std::make_shared<ControlState>(dispatcher_, cluster_manager_,
                                                  upstream_cluster_name, broker_cluster_name);
    auto res = controls_.insert(std::make_pair("", control));
    if (res.second) {
      control->connect();
    }
  }

  void addControlState(const std::string& upstream_cluster_name,
                       Upstream::HostConstSharedPtr broker_host) {
    auto control = std::make_shared<ControlState>(dispatcher_, cluster_manager_,
                                                  upstream_cluster_name, broker_host);
    auto res = controls_.insert(std::make_pair(broker_host->address()->asString(), control));
    if (res.second) {
      control->connect();
    }
  }

  void removeControlState(const std::string& key) {
    auto it = controls_.find(key);
    if (it != controls_.end()) {
      it->second->terminate();
      controls_.erase(it);
    }
  }

  Event::SignalEventPtr sigterm;
  Event::SignalEventPtr sigint;

private:
  Event::Dispatcher& dispatcher_;
  Upstream::ClusterManager& cluster_manager_;
  std::map<std::string, ControlStateSharedPtr> controls_;
};

void ControlState::initializeControlConnection(
    Upstream::Cluster& broker_cluster, const ::envoy::api::v2::Cluster_FrozyConnectorConfig& config,
    Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher) {

  ENVOY_LOG(info, "Frozy connector cluster '{}' (use all hosts: {}) <--> upstrem cluster '{}'",
            broker_cluster.info()->name(), config.connect_to_all(), config.upstream_cluster());

  auto control_manager = std::make_shared<ControlManager>(dispatcher, cluster_manager);

  control_manager->sigterm = dispatcher.listenForSignal(SIGTERM, [control_manager]() {
    ENVOY_LOG(info, "caught SIGTERM on Frozy state");
    control_manager->terminate();
  });

  control_manager->sigint = dispatcher.listenForSignal(SIGINT, [control_manager]() {
    ENVOY_LOG(info, "caught SIGINT on Frozy state");
    control_manager->terminate();
  });

  if (config.connect_to_all()) {
    broker_cluster.prioritySet().addMemberUpdateCb(
        [control_manager,
         upstream = config.upstream_cluster()](uint32_t, const Upstream::HostVector& hosts_added,
                                               const Upstream::HostVector& hosts_removed) {
          ENVOY_LOG(info, "Frozy connector hosts added: {}, removed {}", hosts_added.size(),
                    hosts_removed.size());
          for (const auto& host : hosts_added) {
            ENVOY_LOG(debug, "Added Forzy connector host {}", host->address()->asString());
            control_manager->addControlState(upstream, host);
          }
          for (const auto& host : hosts_removed) {
            ENVOY_LOG(debug, "Removed Frozy connector host {}", host->address()->asString());
            control_manager->removeControlState(host->address()->asString());
          }
        });
  } else {
    control_manager->addControlState(config.upstream_cluster(), broker_cluster.info()->name());
  }
}

void ControlState::connect() {
  reconnect_timer_ = dispatcher_.createTimer(
      [state = shared_from_this()]() -> void { state->createControlConnection(); });
  reconnect_timer_->enableTimer(std::chrono::seconds(0));
}

void ControlState::terminate() {
  ENVOY_LOG(info, "Termnate Frozy control connection state");
  terminate_ = true;
  if (reconnect_timer_) {
    reconnect_timer_->disableTimer();
    reconnect_timer_.reset();
  }
  control_conn_->close(Network::ConnectionCloseType::NoFlush);
  for (auto& cli : downstreams_) {
    cli.second->close(Network::ConnectionCloseType::NoFlush);
  }
  for (auto& cli : upstreams_) {
    cli.second->connection().close(Network::ConnectionCloseType::NoFlush);
  }
  for (auto& cli : upstream_connecting_) {
    cli->cancel_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess);
  }
}

void ControlState::createControlConnection() {
  ASSERT(broker_host_ || !permanent_broker_host_);

  if (!permanent_broker_host_) {
    broker_host_ = chooseHost(broker_cluster_name_);
  }

  if (!broker_host_) {
    ENVOY_LOG(error, "Cannot select a host of cluster {}", broker_cluster_name_);
    reconnectControlConnection();
  } else {
    auto conn = broker_host_->createConnection(
        dispatcher_, broker_host_->cluster().clusterSocketOptions(), nullptr);
    if (!conn.connection_) {
      ENVOY_LOG(error, "Cannot create connection to a host of cluster {}", broker_cluster_name_);
      reconnectControlConnection();
    } else {
      conn.connection_->addReadFilter(std::make_shared<ControlFilter>(shared_from_this()));
      conn.connection_->connect();
      control_conn_ = std::move(conn.connection_);
    }
  }
}

void ControlState::reconnectControlConnection() {
  dispatcher_.post([s = shared_from_this()]() -> void { s->control_conn_.reset(); });
  if (!terminate_ && reconnect_timer_) {
    reconnect_timer_->enableTimer(std::chrono::seconds(2));
  }
}

void ControlState::createUpstreamConnectionOnDispatcher(uint64_t id) {
  dispatcher_.post(
      [state = shared_from_this(), id]() -> void { state->createUpstreamConnection(id); });
}

void ControlState::createUpstreamConnection(uint64_t id) {
  if (getClsuterForConnect(upstream_cluster_name_) == nullptr) {
    ENVOY_LOG(error, "Cannot connect to cluster '{}'", upstream_cluster_name_);
  } else {
    auto conn_pool = cluster_manager_.tcpConnPoolForCluster(
        upstream_cluster_name_, Upstream::ResourcePriority::Default, nullptr, nullptr);
    if (conn_pool == nullptr) {
      ENVOY_LOG(error, "Cannot connect to cluster {}, no healthy upstream", upstream_cluster_name_);
    }

    auto conn = std::make_unique<UpstreamConnecting>(*this, id);
    if (conn->newConnection(conn_pool) != nullptr) {
      conn->moveIntoList(std::move(conn), upstream_connecting_);
    }
  }
}

Network::ClientConnectionPtr ControlState::createDownstreamConnection() {
  if (broker_host_) {
    auto conn = broker_host_->createConnection(
        dispatcher_, broker_host_->cluster().clusterSocketOptions(), nullptr);
    if (conn.connection_) {
      return std::move(conn.connection_);
    }
    ENVOY_LOG(error, "Failed connect to broker host {}", broker_host_->address()->asString());
  }
  return nullptr;
}

void ControlState::removeDownstream(uint64_t id) {
  dispatcher_.post([s = shared_from_this(), id]() -> void { s->downstreams_.erase(id); });
}

void ControlState::removeUpstream(uint64_t id) {
  dispatcher_.post([s = shared_from_this(), id]() -> void { s->upstreams_.erase(id); });
}

Upstream::ThreadLocalCluster* ControlState::getClsuterForConnect(const std::string& cluster_name) {
  if (auto cluster = cluster_manager_.get(cluster_name)) {
    ENVOY_LOG(debug, "Creating connection to cluster '{}'", cluster_name);
    auto info = cluster->info();
    if (info->resourceManager(Upstream::ResourcePriority::Default).connections().canCreate()) {
      return cluster;
    }
  } else {
    ENVOY_LOG(error, "Unknown cluster '{}'", cluster_name);
  }
  return nullptr;
}

Upstream::HostConstSharedPtr ControlState::chooseHost(const std::string& cluster_name) {
  if (auto cluster = getClsuterForConnect(cluster_name)) {
    return cluster->loadBalancer().chooseHost(nullptr);
  }
  ENVOY_LOG(error, "Cannot connect to cluster {}", cluster_name);
  return nullptr;
}

void ControlState::upstreamConnected(Tcp::ConnectionPool::ConnectionDataPtr&& upstream,
                                     uint64_t request) {
  if (auto downstream = createDownstreamConnection()) {
    downstream->addReadFilter(
        std::make_shared<DownstreamFilter>(upstream->connection(), request, shared_from_this()));
    downstream->connect();
    auto upstream_callbacks =
        std::make_unique<UpstreamCallbacks>(std::move(upstream), *downstream, shared_from_this());
    upstream_callbacks->initializeCallbacks();
    downstreams_.emplace(downstream->id(), std::move(downstream));
    upstreams_.emplace(upstream_callbacks->connection().id(), std::move(upstream_callbacks));
  } else {
    upstream->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

Tcp::ConnectionPool::Cancellable*
ControlState::UpstreamConnecting::newConnection(Tcp::ConnectionPool::Instance* pool) {
  return cancel_handle_ = pool->newConnection(*this);
}

void ControlState::UpstreamConnecting::onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                                                     Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(error, "Upstream pool failure on host {}. Reason: {}", host->address()->asString(),
            enumToInt(reason));
  if (inserted()) {
    removeFromList(parent_.upstream_connecting_);
  }
}

void ControlState::UpstreamConnecting::onPoolReady(
    Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
    Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(trace, "Upstream pool ready on host {}", host->address()->asString());
  parent_.upstreamConnected(std::move(conn_data), request_);
  if (inserted()) {
    removeFromList(parent_.upstream_connecting_);
  }
}

} // namespace Frozy
} // namespace Extensions
} // namespace Envoy
