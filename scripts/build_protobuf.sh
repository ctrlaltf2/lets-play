#!/bin/bash

# Builds a copy of protobuf from source.
# Needed until a stable Protobuf version (not crate version!) supports the
# Google Rust implementation of protobuf.

# alter this to where ever you put protobuf sources
# this is where I put mine
# ? clone to /tmp ?
pushd ~/source/protobuf

set -e

# check out the right tag.
git checkout rust-prerelease-4.30.0-beta1

# reconfigure
[[ ! -d "build" ]] && {
	cmake -Wno-dev -Bbuild -GNinja -DCMAKE_INSTALL_PREFIX=$HOME/bin/pbrust \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		-Dprotobuf_FORCE_FETCH_DEPENDENCIES=ON
};

ninja -C build
ninja -C build install

