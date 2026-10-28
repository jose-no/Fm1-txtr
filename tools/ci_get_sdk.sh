#!/bin/bash
# The three JieLi AC79 SDK files the firmware package uses (Apache-2.0, never committed), checked by hash:
# felucca-bsp's get_sdk.sh first (JieLi's GitHub), then JieLi's Gitee repository at the same release tag.
set -e
DEST=${1:-$HOME/.jieli/ac79-sdk}
check() {
    echo "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4  $DEST/cpu/wl82/tools/uboot.boot
276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b  $DEST/cpu/wl82/tools/cfg_tool.bin
41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d  $DEST/cpu/wl82/tools/cfg/eq_cfg_hw.bin" | sha256sum -c -
}
check 2>/dev/null && exit 0
if sh fm1-base/bsp/tools/get_sdk.sh "$DEST"; then check && exit 0; fi
echo "get_sdk.sh failed; trying Gitee" >&2
rm -rf "$DEST"
git clone -q --depth 1 --filter=blob:none --sparse --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$DEST"
git -C "$DEST" sparse-checkout set --no-cone /LICENSE /cpu/wl82/tools/
check
