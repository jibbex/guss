FROM ubuntu

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    gcc \
    git \
    python3 \
    libssl-dev \
    libomp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

ENTRYPOINT ["bash", "-c", "cmake -B cmake-build -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build -j"]