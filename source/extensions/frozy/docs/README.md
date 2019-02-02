# How the Frozy extension works

## Envoy with a Frozy broker cluster on the client side

Sample configuration: [envoy_frozy_cluster.yaml](../../../../configs/envoy_frozy_cluster.yaml).

* Declare a special Frozy Broker cluster in the static configuration with `type: FROZY` and with one or more local broker addresses in `hosts` or `load_assignment` section for accepting incoming TCP connections from the Frozy Connector (see below). The Frozy broker cluster initialize TCP listeners for each broker address.

* For each accepted connection on the configured port the broker reads the first 8 bytes as a `request ID`, where `0` means that the connection is a new Frozy connector control channel, and `not 0` means that the connection is a requested upstream data channel.

* For each control channel connection the broker initializes a new cluster's virtual host that can be selected by the Envoy upper level (cluster load balancer) to create a new upstream connection. The actual number of active Frozy broker cluster hosts is equal to the number of established control channels, regardless of the number of configured broker local ports.

* When a virtual host is called for a new connection, it sends a `request ID` to the corresponding control channel and waits for a new upstream data connection to the same broker port and with the corresponding `request ID`.

* At this point the cluster client connection and corresponding upstream data connection are established and start forwarding TCP payload in both direction.

## Envoy a Frozy connector cluster on the server side

Sample configuration: [envoy_frozy_connector.yaml](../../../../configs/envoy_frozy_connector.yaml).

* Declare a special Frozy Connector cluster in the static configuration with the additional `frozy_connector` section:

        frozy_connector:
          upstream_cluster: <real_upstream_cluster_name>
          connect_to_all: <true|false>

* On startup, If `connect_to_all` flag is `false` or absent, Envoy chooses a new host of the cluster using the cluster load balancer policy and establishes a control channel to the host address. If the control channel connection is unexpectedly terminated it chooses a new host to reconnect.

* If `connect_to_all` flag is `true`, Envoy establishes a control channel connection to all available hosts of the cluster. When a new host is added to the cluster at runtime, a new control connection is inititated to that host. When a host is removed from the cluster, the corresponding control connection is terminated.

* When a control channel receives the upstream `request ID` from the Frozy broker the following will happen:

    1. A new connection to the `upstream cluster` specified in the `frozy_connector` section will be initiated
    2. A new connection to the same Frozy broker port (to which the control channel is connected) will be initiated and the `request ID` will be sent back as a response
    3. A loop will be started to forward TCP payload data between these two connections in both directions
