# Build environment for Space Taxi Mayhem on Linux x86-64.
# Ubuntu 22.04: g++ 11, CMake 3.22, SDL2 2.0.20, SDL2_ttf 2.0.18 (glibc 2.35, so the
# resulting binary runs on Ubuntu 22.04+, Debian 12+, Fedora 36+, SteamOS, ...).
# On Apple Silicon this runs as x86-64 under Docker Desktop's emulation.
FROM --platform=linux/amd64 ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config ca-certificates \
      libsdl2-dev libsdl2-ttf-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
CMD ["/bin/bash"]
