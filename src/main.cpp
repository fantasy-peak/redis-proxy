#include <signal.h>

#include <iostream>

#include "redis_proxy.h"
#include "redis_proxy_config.h"

auto convert(const std::string& log_level) {
	if (log_level == "kTrace")
		return trantor::Logger::LogLevel::kTrace;
	else if (log_level == "kDebug")
		return trantor::Logger::LogLevel::kDebug;
	else if (log_level == "kInfo")
		return trantor::Logger::LogLevel::kInfo;
	else if (log_level == "kWarn")
		return trantor::Logger::LogLevel::kWarn;
	else if (log_level == "kError")
		return trantor::Logger::LogLevel::kError;
	else if (log_level == "kFatal")
		return trantor::Logger::LogLevel::kFatal;
	else if (log_level == "kNumberOfLogLevels")
		return trantor::Logger::LogLevel::kNumberOfLogLevels;
	else
		throw std::bad_cast();
}

std::unique_ptr<RedisProxy> redis_proxy_ptr;

int main(int, char** argv) {
	auto redis_proxy_config = parseFile(argv[1]);
	trantor::Logger::setLogLevel(convert(redis_proxy_config.log_level));
	::signal(SIGINT, [](int) { redis_proxy_ptr->quit(); });
	redis_proxy_ptr = std::make_unique<RedisProxy>(redis_proxy_config);
	redis_proxy_ptr->run();
	return 0;
}
