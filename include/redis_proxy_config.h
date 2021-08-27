#pragma once

#include <yaml-cpp/yaml.h>

struct InetAddress {
	std::string ip;
	uint16_t port;
	bool ipv6 = false;
};

struct RedisProxyConfig {
	InetAddress server_address;
	std::vector<InetAddress> redis_config_vec;
	uint16_t io_thread_num;
	uint16_t redis_io_thread_num;
	uint32_t timeout;
};

inline RedisProxyConfig parseFile(const std::string& config_file) {
	RedisProxyConfig redis_proxy_config;
	YAML::Node config = YAML::LoadFile(config_file);
	auto server_address = config["server_address"].as<InetAddress>();
	auto redis_config_vec = config["redis_config"].as<std::vector<InetAddress>>();
	auto io_thread_num = config["io_thread_num"].as<uint16_t>();
	auto redis_io_thread_num = config["io_thread_num"].as<uint16_t>();
	auto timeout = config["timeout"].as<uint32_t>();
	return RedisProxyConfig{server_address, redis_config_vec, io_thread_num, redis_io_thread_num, timeout};
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
