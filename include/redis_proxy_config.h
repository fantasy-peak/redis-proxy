#pragma once

#include <yaml-cpp/yaml.h>

struct InetAddress {
	std::string ip;
	uint16_t port;
};

struct RedisProxyConfig {
	InetAddress endpoint;
	std::vector<InetAddress> redis_config_vec;
	uint16_t io_thread_num;
	uint32_t timeout;
	uint32_t timeout_loop_interval;
};

inline auto parse_file(const std::string& config_file) {
	RedisProxyConfig redis_proxy_config;
	YAML::Node config = YAML::LoadFile(config_file);
	return std::make_shared<RedisProxyConfig>(
		config["endpoint"].as<InetAddress>(),
		config["redis_config"].as<std::vector<InetAddress>>(),
		config["io_thread_num"].as<uint16_t>(),
		config["timeout"].as<uint32_t>(),
		config["timeout_loop_interval"].as<uint32_t>());
}

namespace YAML {

template <>
struct convert<InetAddress> {
	static bool decode(YAML::Node const& node, InetAddress& rhs) {
		rhs.ip = node["ip"].as<std::string>();
		rhs.port = node["port"].as<uint16_t>();
		return true;
	}
};

} // namespace YAML
