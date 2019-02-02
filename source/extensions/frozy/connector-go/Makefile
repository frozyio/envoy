REGISTRY_IMAGE ?= "frozy/envoy-connector-go"
ifneq ("$(REGISTRY_IMAGE_TAG)", "")
	TAG_SECTION=:$(REGISTRY_IMAGE_TAG)
endif

all: build

build:
	CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -tags netgo -ldflags '-w -s' -o envoy-connector

deps:
	go get -d -v

image:
	docker build -t ${REGISTRY_IMAGE}${TAG_SECTION} .
