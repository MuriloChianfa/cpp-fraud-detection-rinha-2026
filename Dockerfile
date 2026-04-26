# syntax=docker/dockerfile:1.6
#
# Multi-stage build for the rinha-2026-cpp-drogon image.
#
#   - Stage `drogon-build`: builds Drogon (and its bundled Trantor) from
#     source against musl/Alpine 3.20. Pinned to a recent release tag so the
#     build is reproducible. We disable everything we don't use (ORM, ctl,
#     examples, all DBs, brotli, yaml config) to keep the image lean.
#   - Stage `app-build`:    builds the api + lb binaries with -O3 -flto and
#     -march=haswell, runs prepare_refs at build time to bake the quantised
#     reference dataset into the binary.
#   - Stage `runtime`:      alpine + just the .so files we actually need
#     (drogon, trantor, jsoncpp, libstdc++, ...). Final image ~30 MB.
#
# Always built --platform=linux/amd64 because the contest test machine is
# a Late-2014 Mac mini (Haswell, AVX2).

ARG ALPINE_VERSION=3.20
ARG DROGON_VERSION=v1.9.7

# ============================ Drogon =========================================

FROM --platform=linux/amd64 alpine:${ALPINE_VERSION} AS drogon-build

RUN apk add --no-cache \
        cmake make ninja g++ git \
        linux-headers musl-dev \
        jsoncpp-dev zlib-dev openssl-dev c-ares-dev util-linux-dev

ARG DROGON_VERSION
WORKDIR /tmp
RUN git clone --depth 1 --branch ${DROGON_VERSION} \
        --recurse-submodules https://github.com/drogonframework/drogon.git
WORKDIR /tmp/drogon/build

RUN cmake .. -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_CTL=OFF \
        -DBUILD_ORM=OFF \
        -DBUILD_TESTING=OFF \
        -DBUILD_BROTLI=OFF \
        -DBUILD_YAML_CONFIG=OFF \
        -DUSE_POSTGRESQL=OFF \
        -DUSE_MYSQL=OFF \
        -DUSE_SQLITE3=OFF \
        -DUSE_REDIS=OFF \
        -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG -fvisibility=hidden" \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && ninja -j"$(nproc)" \
    && ninja install \
    && strip --strip-unneeded /usr/local/lib/libdrogon.so* /usr/local/lib/libtrantor.so* || true

# ============================ App ============================================

FROM --platform=linux/amd64 drogon-build AS app-build

WORKDIR /src
COPY CMakeLists.txt info.json /src/
COPY src       /src/src
COPY tools     /src/tools
COPY resources /src/resources
COPY cmake     /src/cmake

RUN cmake -S /src -B /src/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /src/build --parallel "$(nproc)" \
    && strip --strip-unneeded /src/build/api /src/build/lb

# ============================ Runtime ========================================

FROM --platform=linux/amd64 alpine:${ALPINE_VERSION} AS runtime

# Runtime deps for Drogon's shared lib: jsoncpp, openssl, libuuid, c-ares,
# zlib, plus libstdc++ for our binaries.
RUN apk add --no-cache \
        jsoncpp openssl libuuid c-ares zlib libstdc++ \
    && rm -rf /var/cache/apk/* /tmp/* /var/tmp/*

COPY --from=app-build /usr/local/lib/libdrogon.so* /usr/local/lib/
COPY --from=app-build /usr/local/lib/libtrantor.so* /usr/local/lib/
COPY --from=app-build /src/build/api /api
COPY --from=app-build /src/build/lb  /lb

# Make sure the dynamic loader can find /usr/local/lib without ldconfig.
ENV LD_LIBRARY_PATH=/usr/local/lib

ENTRYPOINT []
CMD ["/api", "8001"]
