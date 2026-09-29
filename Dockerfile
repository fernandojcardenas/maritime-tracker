# maritime-tracker with its live map, in one small image.
#
#   docker build -t maritime-tracker .
#   docker run --rm -p 8080:8080 maritime-tracker            # replays the bundled sample
#   docker run --rm -p 8080:8080 -e BW_CLIENT_ID -e BW_CLIENT_SECRET maritime-tracker   # live Norwegian AIS
#
# then open http://localhost:8080/. See docs/live-map.md.

ARG BASE=ubuntu:24.04

FROM ${BASE} AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake ninja-build ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake cmake
COPY include include
COPY src src
COPY apps apps
COPY web web
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMT_BUILD_TESTS=OFF \
 && cmake --build build --target mt-ingest \
 && strip build/mt-ingest

FROM ${BASE}
# curl and python3 are for tools/barentswatch_stream.sh (live mode).
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --no-create-home --shell /usr/sbin/nologin tracker
COPY --from=build /src/build/mt-ingest /usr/local/bin/mt-ingest
COPY tools/barentswatch_stream.sh /usr/local/bin/barentswatch_stream.sh
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
COPY testdata/barentswatch-live-2026-09-29.jsonl /usr/share/maritime-tracker/sample.jsonl
USER tracker
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
