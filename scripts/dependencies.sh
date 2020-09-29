#!/bin/bash -x

git clone  -b v1.5.2 --depth 1 https://github.com/google/benchmark.git
git clone -b release-1.10.0 --depth 1  https://github.com/google/googletest.git benchmark/googletest