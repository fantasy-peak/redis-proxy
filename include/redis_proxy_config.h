#pragma once

#include <yaml-cpp/yaml.h>

struct InetAddress {
	std::string ip;
	uint16_t port;
	bool ipv6 = false;
};

struct RedisProxyConfig {
	InetAddress endpoint;
	std::vector<InetAddress> redis_config_vec;
	uint16_t io_thread_num;
	uint16_t redis_io_thread_num;
	uint32_t timeout;
	uint32_t poll_interval;
	std::string log_level;
};

inline RedisProxyConfig parseFile(const std::string& config_file) {
	RedisProxyConfig redis_proxy_config;
	YAML::Node config = YAML::LoadFile(config_file);
	return RedisProxyConfig{
		config["endpoint"].as<InetAddress>(),
		config["redis_config"].as<std::vector<InetAddress>>(),
		config["io_thread_num"].as<uint16_t>(),
		config["redis_io_thread_num"].as<uint16_t>(),
		config["timeout"].as<uint32_t>(),
		config["poll_interval"].as<uint32_t>(),
		config["log_level"].as<std::string>(),
	};
}

namespace YAML {

template <>
struct convert<InetAddress> {
	static bool decode(YAML::Node const& node, InetAddress& rhs) {
		rhs.ip = node["ip"].as<std::string>();
		rhs.port = node["port"].as<uint16_t>();
		rhs.ipv6 = node["ipv6"].as<bool>();
		return true;
	}
};

} // namespace YAML
