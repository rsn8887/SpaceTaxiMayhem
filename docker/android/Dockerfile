# Build environment for the Space Taxi Mayhem Android APK.
# Ubuntu 22.04 + OpenJDK 17, Android SDK 34, NDK r26d, CMake 3.22.1, Gradle 8.7,
# SDL2 2.30.12 and SDL2_ttf 2.22.0 sources. Usage: see README (runs android/build.sh).
# The Android NDK only exists for x86-64 Linux hosts, so on Apple Silicon this runs under
# Docker Desktop's x86-64 emulation (slower, but works).
FROM --platform=linux/amd64 ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      openjdk-17-jdk-headless curl unzip ca-certificates git \
 && rm -rf /var/lib/apt/lists/*

ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ARG CMDLINE_TOOLS=11076708
RUN mkdir -p $ANDROID_HOME/cmdline-tools \
 && curl -fsSL -o /tmp/tools.zip https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip \
 && unzip -q /tmp/tools.zip -d $ANDROID_HOME/cmdline-tools \
 && mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest \
 && rm /tmp/tools.zip
ENV PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin
RUN yes | sdkmanager --licenses > /dev/null \
 && sdkmanager "platforms;android-34" "build-tools;34.0.0" "ndk;26.3.11579264" "cmake;3.22.1" > /dev/null

ARG GRADLE=8.7
RUN curl -fsSL -o /tmp/gradle.zip https://services.gradle.org/distributions/gradle-${GRADLE}-bin.zip \
 && unzip -q /tmp/gradle.zip -d /opt \
 && ln -s /opt/gradle-${GRADLE}/bin/gradle /usr/local/bin/gradle \
 && rm /tmp/gradle.zip

ARG SDL2=2.30.12
ARG SDL2_TTF=2.22.0
ENV ANDROID_DEPS=/opt/android-deps
RUN mkdir -p $ANDROID_DEPS && cd $ANDROID_DEPS \
 && curl -fsSL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2}/SDL2-${SDL2}.tar.gz | tar xz \
 && curl -fsSL https://github.com/libsdl-org/SDL_ttf/releases/download/release-${SDL2_TTF}/SDL2_ttf-${SDL2_TTF}.tar.gz | tar xz \
 && ln -s SDL2-${SDL2} SDL && ln -s SDL2_ttf-${SDL2_TTF} SDL_ttf \
 && if [ ! -f SDL_ttf/external/freetype/CMakeLists.txt ]; then cd SDL_ttf/external && sh ./download.sh; fi

WORKDIR /app
CMD ["/bin/bash"]
