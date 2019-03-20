#include "extensions/frozy/upstream/cluster.h"
#include "extensions/frozy/upstream/downstream_connection.h"
#include "extensions/frozy/upstream/upstream_connection.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Upstream {
namespace Frozy {

FrozyClusterImpl::FrozyClusterImpl(
    const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
    Runtime::RandomGenerator& random,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : BaseDynamicClusterImpl(cluster, runtime, factory_context, std::move(stats_scope),
                             added_via_api),
      local_info_(factory_context.localInfo()), dispatcher_(dispatcher), random_(random) {

  const envoy::api::v2::ClusterLoadAssignment cluster_load_assignment(
      cluster.has_load_assignment() ? cluster.load_assignment()
                                    : Config::Utility::translateClusterHosts(cluster.hosts()));

  overprovisioning_factor_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      cluster_load_assignment.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);

  for (const auto& locality_lb_endpoint : cluster_load_assignment.endpoints()) {
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      auto address = resolveProtoAddress(lb_endpoint.endpoint().address());
      upstream_listeners_.emplace_back(
          std::make_unique<UpstreamListener>(*this, address, lb_endpoint, locality_lb_endpoint));
    }
  }
}

void FrozyClusterImpl::startPreInit() { onPreInitComplete(); }

void FrozyClusterImpl::updateAllHosts(const HostVector& hosts_added,
                                      const HostVector& hosts_removed, uint32_t current_priority) {
  PriorityStateManager priority_state_manager(*this, local_info_, nullptr);

  for (const UpstreamListenerPtr& listener : upstream_listeners_) {
    priority_state_manager.initializePriorityFor(listener->locality_lb_endpoint_);
    ENVOY_LOG(debug, "Frozy upstream listener on {} hosts count: {}",
              listener->address()->asString(), listener->hosts_.size());
    for (const auto& host : listener->hosts_) {
      if (listener->locality_lb_endpoint_.priority() == current_priority) {
        priority_state_manager.registerHostForPriority(host.second, 
                                                       listener->locality_lb_endpoint_);
      }
    }
  }

  priority_state_manager.updateClusterPrioritySet(
      current_priority, std::move(priority_state_manager.priorityState()[current_priority].first),
      hosts_added, hosts_removed, absl::nullopt, overprovisioning_factor_);

  onPreInitComplete();
}

FrozyClusterImpl::Listener::Listener(FrozyClusterImpl& cluster, Network::SocketPtr&& socket,
                                     UpstreamControlSharedPtr control)
    : cluster_(cluster), socket_(std::move(socket)),
      listener_(cluster_.dispatcher_.createListener(*socket_, *this, true, false)),
      control_(control) {
  ENVOY_LOG(debug, "New listener for Frozy on {}", socket_->localAddress()->asString());
}

FrozyClusterImpl::Listener::~Listener() {
  if (address()->type() == Network::Address::Type::Pipe) {
    int err = std::remove(address()->asString().c_str());
    if (err != 0) {
      ENVOY_LOG(warn, "Failed to remvoe sock file {}. Error: {}.", address()->asString(), err);
    }
  }
}

void FrozyClusterImpl::Listener::onAccept(Network::ConnectionSocketPtr&& socket,
                                          bool /* hand_off_restored_destination_connections*/) {
  auto new_connection = cluster_.dispatcher_.createServerConnection(
      std::move(socket), cluster_.info_->transportSocketFactory().createTransportSocket(nullptr));
  new_connection->setBufferLimits(cluster_.info_->perConnectionBufferLimitBytes());
  onNewConnection(std::move(new_connection));
}

FrozyClusterImpl::DownstreamListener::DownstreamListener(
    FrozyClusterImpl& cluster, Network::Address::InstanceConstSharedPtr& address,
    UpstreamControlSharedPtr control, uint64_t control_conn_id)
    : Listener(cluster, std::make_unique<Network::UdsListenSocket>(address), std::move(control)),
      control_conn_id_(control_conn_id) {}

void FrozyClusterImpl::DownstreamListener::onNewConnection(
    Network::ConnectionPtr&& new_connection) {
  ASSERT(new_connection && new_connection.get());
  ENVOY_CONN_LOG(trace, "Frozy proxy listener acceped connection on {}, buffer_limit: {}",
                 *new_connection, new_connection->localAddress()->asString(),
                 new_connection->bufferLimit());

  auto downstream = std::make_shared<DownstreamConnection>(std::move(new_connection), control_);
  control_->newConnection(downstream);
  if (auto conn_timer = control_->registerDownstream(downstream, control_conn_id_)) {
    conn_timer->enableTimer(cluster_.info_->connectTimeout());
    downstream->setTimer(std::move(conn_timer));
  }
}

FrozyClusterImpl::UpstreamListener::UpstreamListener(
    FrozyClusterImpl& cluster, Network::Address::InstanceConstSharedPtr& address,
    const envoy::api::v2::endpoint::LbEndpoint& lb_endpoint,
    const envoy::api::v2::endpoint::LocalityLbEndpoints& locality_lb_endpoint)
    : Listener(cluster,
               std::make_unique<Network::TcpListenSocket>(
                   address, std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>(),
                   true),
               nullptr),
      lb_endpoint_(lb_endpoint), locality_lb_endpoint_(locality_lb_endpoint) {
  control_ = std::make_shared<UpstreamControl>(*this);
}

void FrozyClusterImpl::UpstreamListener::onNewConnection(Network::ConnectionPtr&& new_connection) {
  ASSERT(new_connection && new_connection.get());
  ENVOY_CONN_LOG(trace, "Frozy upstream listener accepted connection on {}, buffer_limit: {}",
                 *new_connection, new_connection->localAddress()->asString(),
                 new_connection->bufferLimit());
  auto upstream = std::make_shared<UpstreamConnection>(std::move(new_connection), control_);
  control_->newConnection(upstream);
  upstream->connection().addReadFilter(upstream);
}

void FrozyClusterImpl::UpstreamListener::onControlConnected(uint64_t control_conn_id) {
  auto address = DownstreamListener::newAddress(cluster_.random_.uuid());

  downstream_listeners_[control_conn_id] =
      std::make_unique<DownstreamListener>(cluster_, address, control_, control_conn_id);

  auto host = std::make_shared<HostImpl>(
      cluster_.info(), "", address, lb_endpoint_.metadata(),
      lb_endpoint_.load_balancing_weight().value(), locality_lb_endpoint_.locality(),
      lb_endpoint_.endpoint().health_check_config(), locality_lb_endpoint_.priority(),
      envoy::api::v2::core::HealthStatus::UNKNOWN);

  hosts_[control_conn_id] = host;
  ENVOY_LOG(debug, "Frozy hosts added for upstream listener on {}", this->address()->asString());
  cluster_.updateAllHosts(HostVector{host}, HostVector{}, locality_lb_endpoint_.priority());
}

void FrozyClusterImpl::UpstreamListener::onControlClosure(uint64_t control_conn_id) {
  auto it = hosts_.find(control_conn_id);
  if (it != hosts_.end()) {
    auto host = it->second;
    hosts_.erase(it);
    ENVOY_LOG(debug, "Frozy hosts removed for upstream listener on {}", address()->asString());
    cluster_.updateAllHosts(HostVector{}, HostVector{host}, locality_lb_endpoint_.priority());
  }

  downstream_listeners_.erase(control_conn_id);
}

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy
