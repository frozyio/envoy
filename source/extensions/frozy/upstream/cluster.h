#pragma once

#include "common/upstream/upstream_impl.h"
#include "common/common/linked_object.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/address_impl.h"
#include "extensions/frozy/upstream/state.h"

namespace Envoy {
namespace Upstream {
namespace Frozy {

class FrozyClusterImpl : public BaseDynamicClusterImpl {
public:
  FrozyClusterImpl(const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
                   Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                   Server::Configuration::TransportSocketFactoryContext& factory_context,
                   Stats::ScopePtr&& stats_scope, bool added_via_api);

  // Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }

private:
  struct Listener : public Network::ListenerCallbacks {
    Listener(FrozyClusterImpl& cluster, Network::SocketPtr&& socket,
             UpstreamControlSharedPtr control);
    virtual ~Listener();

    const Network::Address::InstanceConstSharedPtr& address() const {
      return socket_->localAddress();
    }

    // Network::ListenerCallbacks
    void onAccept(Network::ConnectionSocketPtr&& socket,
                  bool hand_off_restored_destination_connections = true) override;

  protected:
    FrozyClusterImpl& cluster_;
    Network::SocketPtr socket_;
    Network::ListenerPtr listener_;
    UpstreamControlSharedPtr control_;
  };
  typedef std::unique_ptr<Listener> ListenerPtr;

  struct DownstreamListener : public Listener {
    DownstreamListener(FrozyClusterImpl&, Network::Address::InstanceConstSharedPtr&,
                       UpstreamControlSharedPtr, uint64_t);
    void onNewConnection(Network::ConnectionPtr&&) override;

    static Network::Address::InstanceConstSharedPtr newAddress(const std::string& id) {
      return Network::Address::InstanceConstSharedPtr{
          new Network::Address::PipeInstance(fmt::format("/tmp/envoy_frozy_{}.sock", id))};
    }

    const uint64_t control_conn_id_;
  };

  struct UpstreamListener : public Listener, public UpstreamControlCallbacks {
    UpstreamListener(FrozyClusterImpl&, Network::Address::InstanceConstSharedPtr&,
                     const envoy::api::v2::endpoint::LbEndpoint&,
                     const envoy::api::v2::endpoint::LocalityLbEndpoints&);

    void onNewConnection(Network::ConnectionPtr&&) override;

    void onControlConnected(uint64_t id) override;
    void onControlClosure(uint64_t id) override;

    const envoy::api::v2::endpoint::LbEndpoint lb_endpoint_;
    const envoy::api::v2::endpoint::LocalityLbEndpoints locality_lb_endpoint_;
    std::map<uint64_t, HostSharedPtr> hosts_;
    std::map<uint64_t, ListenerPtr> downstream_listeners_;
  };
  typedef std::unique_ptr<UpstreamListener> UpstreamListenerPtr;

  // ClusterImplBase
  void startPreInit() override;

  void updateAllHosts(const HostVector& hosts_added, const HostVector& hosts_removed,
                      uint32_t priority);

  const LocalInfo::LocalInfo& local_info_;
  Event::Dispatcher& dispatcher_;
  Runtime::RandomGenerator& random_;
  uint32_t overprovisioning_factor_;

  std::list<UpstreamListenerPtr> upstream_listeners_;
};

} // namespace Frozy
} // namespace Upstream
} // namespace Envoy