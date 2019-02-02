# build stage
FROM golang:1.11.3 AS build-env
WORKDIR /src
ADD . frozy-envoy-connector/
WORKDIR frozy-envoy-connector
RUN make deps
RUN make build

# Broker itself
FROM alpine:3.8
RUN apk update && apk add ca-certificates
RUN addgroup -g 1000 connector && \
    adduser -D -u 1000 -G connector connector
USER connector
COPY --chown=connector:connector --from=build-env \
  /src/frozy-envoy-connector/envoy-connector \
  /home/connector/envoy-connector
COPY --chown=connector:connector --from=build-env \
  /src/frozy-envoy-connector/frozy-envoy-connector.yaml \
  /home/connector/.frozy-envoy-connector.yaml
ENTRYPOINT ["/home/connector/envoy-connector"]
