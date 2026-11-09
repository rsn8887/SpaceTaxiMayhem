#!/bin/sh
# Builds the Space Taxi Mayhem Android APK (run inside the docker/android image, see README).
#
#   sh ./spacetaxi/android/build.sh [output folder]      (default: build-android next to the project)
#
# Everything is generated in the output folder; the project folder is not touched:
#   <out>/SpaceTaxiMayhem-<version>.apk     the installable APK
#   <out>/spacetaxi-release.keystore        signing key, created on the first build. Keep it:
#                                           updates of an installed APK must use the same key.
#   <out>/project/                          the Gradle project (with SDL sources linked in)
#
# Optional environment variables:
#   ANDROID_DEPS          folder with the SDL and SDL_ttf sources (the image sets /opt/android-deps)
#   ANDROID_KEYSTORE, ANDROID_KEYSTORE_PASSWORD, ANDROID_KEY_ALIAS, ANDROID_KEY_PASSWORD
#                         use your own signing key instead
set -e

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-"$SRC/../build-android"}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
DEPS=${ANDROID_DEPS:-/opt/android-deps}
WORK="$OUT/project"

VER=$(sed -n 's/^project(SpaceTaxiMayhem VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")
[ -n "$VER" ] || { echo "could not read the version from CMakeLists.txt"; exit 1; }

for d in SDL SDL_ttf; do
    [ -f "$DEPS/$d/CMakeLists.txt" ] || { echo "missing $DEPS/$d (set ANDROID_DEPS)"; exit 1; }
done
if [ ! -f "$DEPS/SDL_ttf/external/freetype/CMakeLists.txt" ]; then
    (cd "$DEPS/SDL_ttf/external" && sh ./download.sh)
fi

# Gradle project = android/ from the project + SDL's Java glue + links to the native sources
mkdir -p "$WORK"
cp -R "$SRC/android/." "$WORK/"
rm -f "$WORK/build.sh"
mkdir -p "$WORK/app/src/main/java"
cp -R "$DEPS/SDL/android-project/app/src/main/java/org" "$WORK/app/src/main/java/"
ln -sfn "$DEPS/SDL" "$WORK/app/jni/SDL"
ln -sfn "$DEPS/SDL_ttf" "$WORK/app/jni/SDL_ttf"

# Signing key
KS=${ANDROID_KEYSTORE:-"$OUT/spacetaxi-release.keystore"}
KSPASS=${ANDROID_KEYSTORE_PASSWORD:-spacetaxi}
KALIAS=${ANDROID_KEY_ALIAS:-spacetaxi}
KPASS=${ANDROID_KEY_PASSWORD:-$KSPASS}
if [ ! -f "$KS" ]; then
    echo "Creating signing key $KS"
    keytool -genkeypair -keystore "$KS" -storepass "$KSPASS" -keypass "$KPASS" -alias "$KALIAS" \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=rsn8887" >/dev/null
fi

gradle --no-daemon -p "$WORK" assembleRelease \
    -PgameSrc="$SRC" -PappVersion="$VER" \
    -PkeystoreFile="$KS" -PkeystorePassword="$KSPASS" -PkeyAlias="$KALIAS" -PkeyPassword="$KPASS"

cp "$WORK/app/build/outputs/apk/release/app-release.apk" "$OUT/SpaceTaxiMayhem-$VER.apk"
echo "Built $OUT/SpaceTaxiMayhem-$VER.apk"
