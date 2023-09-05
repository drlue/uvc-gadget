#!/bin/bash
rm -rf build
#rm -rf arm64.meson
cat arm64.meson.template | sed "s|\$NDK|$ANDROID_NDK_ROOT|g" > arm64.meson
meson build --cross=arm64.meson
ninja -C build
