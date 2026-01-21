FROM ubuntu:22.04

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

# GitHub Actionsでビルド済みのバイナリをコピー
COPY build/alsa_streamer /usr/local/bin/alsa_streamer
COPY build/zmq_control_server /usr/local/bin/zmq_control_server
COPY web /opt/totton-dsp/web
COPY data /opt/totton-dsp/data
COPY scripts /opt/totton-dsp/scripts
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
