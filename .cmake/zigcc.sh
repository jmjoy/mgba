#!/bin/sh
# LoongArch64 musl linker wrapper (LA264: no LSX/LASX!)
: "${ZIG_GLOBAL_CACHE_DIR:=/tmp/opencode-zig-cache}"
export ZIG_GLOBAL_CACHE_DIR
: "${ZIG_LOCAL_CACHE_DIR:=/tmp/opencode-zig-local-cache}"
export ZIG_LOCAL_CACHE_DIR
exec zig cc -target loongarch64-linux-musl -mcpu=la64v1_0 -mno-lsx -mno-lasx "$@"
