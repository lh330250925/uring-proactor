FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    pkg-config \
    liburing-dev \
    ca-certificates \
    git \
    cmake \
    libnuma-dev \
    && rm -rf /var/lib/apt/lists/*

ENV PATH="/opt/venv/bin:$PATH"
WORKDIR /app
COPY . /app

CMD ["bash"]
