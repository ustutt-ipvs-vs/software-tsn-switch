FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . src
RUN g++ -O2 -o traffic_source src/traffic_source.cpp && \
    g++ -O2 -o traffic_sink src/traffic_sink.cpp
CMD ["/bin/bash"]
