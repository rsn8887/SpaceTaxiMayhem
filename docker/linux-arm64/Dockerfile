# Build environment for Space Taxi Mayhem on Linux ARM64 (aarch64), e.g. Raspberry Pi 4/5
# running 64-bit Raspberry Pi OS (Bookworm) or Ubuntu.
# Ubuntu 22.04: g++ 11, CMake 3.22, SDL2 2.0.20, SDL2_ttf 2.0.18 (glibc 2.35, so the
# resulting binary runs on Raspberry Pi OS Bookworm 64-bit, Ubuntu 22.04+, Debian 12+, ...).
# On an Apple Silicon Mac this runs natively (no emulation).
FROM --platform=linux/arm64 ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config ca-certificates \
      libsdl2-dev libsdl2-ttf-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
CMD ["/bin/bash"]
