FROM debian:12-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        git \
        ca-certificates \
        python3 \
        python3-pip \
        libasound2-dev \
        libzmq3-dev \
        libvulkan-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/totton-dsp
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_VULKAN=ON -DUSE_VKFFT=ON \
    -DENABLE_ALSA=ON -DENABLE_ZMQ=ON \
    && cmake --build build -j"$(nproc)"

FROM debian:12-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        python3 \
        python3-pip \
        libasound2 \
        libzmq5 \
        libvulkan1 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /opt/totton-dsp

COPY --from=build /opt/totton-dsp/build/alsa_streamer /usr/local/bin/alsa_streamer
COPY --from=build /opt/totton-dsp/build/zmq_control_server /usr/local/bin/zmq_control_server
COPY --from=build /opt/totton-dsp/web /opt/totton-dsp/web
COPY --from=build /opt/totton-dsp/data /opt/totton-dsp/data
COPY docker/entrypoint.sh /usr/local/bin/totton-entrypoint.sh

RUN pip3 install --no-cache-dir \
        fastapi \
        uvicorn \
        jinja2 \
        python-multipart \
        pyzmq \
    && chmod +x /usr/local/bin/totton-entrypoint.sh

EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/totton-entrypoint.sh"]
