FROM varnish:7.4

USER root
COPY . /workdir
WORKDIR /workdir
RUN set -ex; \
    apt-get update; \
    apt-get install -qq cmake docutils jq libcjson1 libcjson-dev pkg-config; \
    cmake -B build; \
    cmake --build build/; \
    ctest --test-dir build/; \
    cmake --install build/; \
    apt-get remove -qq cmake docutils jq libcjson-dev pkg-config; \
    rm -rf /var/lib/apt/lists/* /workdir;
USER varnish
