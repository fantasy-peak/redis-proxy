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
## Test Result
```
// redis-benchmark send request to redis-proxy
➜ ~ redis-benchmark -q -h 127.0.0.1 -p 1234 -d 500
PING_INLINE: 53022.27 requests per second
PING_BULK: 53361.79 requests per second
SET: 52938.06 requests per second
GET: 53219.80 requests per second
INCR: 52659.29 requests per second
LPUSH: 54824.56 requests per second
RPUSH: 55555.56 requests per second
LPOP: 55096.42 requests per second
RPOP: 49875.31 requests per second
SADD: 55279.16 requests per second
HSET: 53792.36 requests per second
SPOP: 54734.54 requests per second
LPUSH (needed to benchmark LRANGE): 54466.23 requests per second
LRANGE_100 (first 100 elements): 12447.10 requests per second
LRANGE_300 (first 300 elements): 4870.45 requests per second
LRANGE_500 (first 450 elements): 3362.25 requests per second
LRANGE_600 (first 600 elements): 2548.49 requests per second
MSET (10 keys): 44267.38 requests per second

// redis-benchmark send request to redis
➜ ~ redis-benchmark -q -h 127.0.0.1 -p 2008 -d 500
PING_INLINE: 58719.91 requests per second
PING_BULK: 58788.95 requests per second
SET: 54704.60 requests per second
GET: 59031.88 requests per second
INCR: 59523.81 requests per second
LPUSH: 56980.06 requests per second
RPUSH: 55803.57 requests per second
LPOP: 52465.90 requests per second
RPOP: 56211.35 requests per second
SADD: 58004.64 requests per second
HSET: 56980.06 requests per second
SPOP: 53908.36 requests per second
LPUSH (needed to benchmark LRANGE): 55710.31 requests per second
LRANGE_100 (first 100 elements): 14208.58 requests per second
LRANGE_300 (first 300 elements): 6028.82 requests per second
LRANGE_500 (first 450 elements): 3386.96 requests per second
LRANGE_600 (first 600 elements): 2418.85 requests per second
MSET (10 keys): 51046.45 requests per second
```