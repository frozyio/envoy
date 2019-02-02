# Frozy Upstream Connector for Envoy

Frozy Upstream Connector is a sidecar proxy for establishing connection from Envoy proxy
to an upstream service in private network. It connects to Envoy FROZY cluster, receives
requests for connection from Envoy, and redirects traffic to the upstream service.

    Downstream ——> Envoy with FROZY cluster <—— Frozy Upstream Connector ——> Upstream

## Configuration

Configuration contains two section: `envoy` is an address where Envoy configured with
`FROZY` cluster; and `service` is an address to the provided server where downstream
connection should be forwarded.

    envoy:
      host: envoy.hostname.or.ip
      port: 7070
    service:
      host: localhost
      port: 4040

Configuration file reads from `$HOME/.frozy-envoy-connector.yaml` or could be specified
via command line argument (see below).

## Build and run with Docker

Configuration file will be copied from ./frozy-envoy-connector.yaml

    make image
    docker run --rm -it --net=host frozy/envoy-connector-go:latest

## Build and run in local environments

It requires `Golang 1.11` compiler and `make` tool.

    make deps
    make build
    ./envoy-connector --config frozy-envoy-connector.yaml
