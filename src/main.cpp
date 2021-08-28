#include <signal.h>

#include <iostream>

#include "redis_proxy.h"
#include "redis_proxy_config.h"

std::unique_ptr<RedisProxy> redis_proxy_ptr;

int main(int, char** argv) {
	auto redis_proxy_config = parseFile(argv[1]);
	::signal(SIGINT, [](int) { redis_proxy_ptr->quit(); });
	redis_proxy_ptr = std::make_unique<RedisProxy>(redis_proxy_config);
	redis_proxy_ptr->run();
	return 0;
}
