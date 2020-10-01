#!/bin/bash -x

mkdir build
cd build
cmake .. -GNinja
ninja
