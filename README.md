# Aviary

## Disclaimer

This is not an officially supported Google product.

Teams from across Google, including Ads teams, are actively engaged in industry
dialog about new technologies that can ensure a healthy ecosystem and preserve
core business models. Online discussions (e.g. on GitHub) of technology
proposals should not be interpreted as commitments about Google ads products.

## Overview

This code provides a server that can run JavaScript bidding and ad scoring
functions, as specified in the [FLEDGE
explainer](https://github.com/WICG/turtledove/blob/main/FLEDGE.md).  The idea is
to
[experiment](https://github.com/WICG/turtledove/issues/154#issuecomment-805811596)
with moving evaluation of JS from on-device to a server instead.

Discussions of how to add trust to the server are not covered here,
[see](https://github.com/google/ads-privacy/blob/master/trust-model/trust_techniques.md).

### Configuring bidding functions

Bidding functions are currently statically configured in a
`server/sample_configuration.yaml` file that gets built into the container.
Edit the file to add or change the bidding function that Aviary should be able
to run, for example:

```yaml
biddingFunctions:
  squaring: |
    (function(inputs) { return inputs.perBuyerSignals.contextualCpm * return inputs.perBuyerSignals.contextualCpm; })
```

### Local development

#### Development environment

In order to be able to build and run Aviary locally, a number of tools need to be installed on the local machine.

1. Install [Python](https://www.python.org/downloads/) and [golang](https://golang.org/doc/install).
2. Run `setup.sh` convenience script to set up the local development environment. It performs the following steps:

   - Install [Docker](http://docker.io).
   - Install [Bazelisk](https://github.com/bazelbuild/bazelisk) – convenience launcher for Bazel.
   - Install [Chromium](https://www.chromium.org)/[V8](http://v8.dev) build dependencies.

#### Building Aviary

An Aviary server is built as a Docker container.

One can build it with:

```console
bazelisk run //server:aviary
```

or, if you're not using Bazelisk:

```console
bazel run //server:aviary
```

#### Running Aviary locally

You can run Aviary locally with Docker and bind to a chosen local HTTP port:

```console
export PORT=8080; docker run -p ${PORT}:${PORT} -e PORT=${PORT} bazel/server:aviary
```

To test that Aviary is running, we can send a request to invoke a simple bidding 
function that doubles the value from `perBuyerSignals`:

```console
curl -X POST http://localhost:8080/v1alpha/adAuctions:computeBid -d '{"biddingFunctionName": "doubling","input": {"perBuyerSignals": {"contextualCpm": 1.23}}}' -H "Content-type: application/json"
{
 "bid": 2.46
}
```

## Maintenance

This code is published so that it‘s possible for anyone to re-run the
experiments that we’re doing. This code will not be supported once the
experimentation is complete.
