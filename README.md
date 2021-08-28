# redis-proxy
Forward a request to multiple redis

## Overview
a redis proxy service, Forward a request to multiple redis, using C++20.

## suported platforms
- Linux

## Build
```shell
https://github.com/fantasy-peak/redis-proxy.git
cd redis-proxy
mkdir build
cd build 
cmake ..
make -j 4
./redis-proxy ../cfg/redis-proxy.yaml
```