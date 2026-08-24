#!/bin/sh
: "${ZIG_GLOBAL_CACHE_DIR:=/tmp/zig-cache}"
export ZIG_GLOBAL_CACHE_DIR
exec zig ranlib "$@"
