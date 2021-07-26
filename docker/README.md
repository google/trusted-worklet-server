# Aviary base docker image

The base docker image specified by `Dockerfile`
contains [Envoy proxy](https://www.envoyproxy.io/) and an up-to-date version of
libstdc++ for binaries built
with [GCC 9](https://www.gnu.org/software/gcc/gcc-9/).

To build and push this container, you can run:

```shell
docker build -t gcr.io/ads-trusted-server-dev/aviary-base . &&
docker push gcr.io/ads-trusted-server-dev/aviary-base:latest
```
