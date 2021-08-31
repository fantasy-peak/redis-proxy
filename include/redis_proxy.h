#pragma once

#include <shared_mutex>

#include <trantor/net/TcpServer.h>

#include "redis_client.h"
#include "redis_proxy_config.h"
#include "redis_reply.h"

class RedisProxy final {
public:
	using RedisClientTuple = std::tuple<std::vector<std::shared_ptr<RedisClient>>, std::unique_ptr<redis_reply::ReplyBuilder>>;

	RedisProxy(const RedisProxyConfig&);
	~RedisProxy();

	void run();
	void quit();

private:
	void onMessage(const trantor::TcpConnectionPtr&, trantor::MsgBuffer*);
	void onConnection(const trantor::TcpConnectionPtr&);
	bool createRedisClient(const trantor::TcpConnectionPtr&);

	RedisProxyConfig m_redis_proxy_config;
	std::vector<trantor::InetAddress> m_redis_inet_address;
	trantor::EventLoop m_server_event_loop_thread;
	std::unique_ptr<trantor::TcpServer> m_tcp_server_ptr;
	std::unique_ptr<trantor::EventLoopThreadPool> m_loop_redis_thread_pool_ptr;
	std::shared_mutex m_mtx;
	std::unordered_map<trantor::TcpConnectionPtr, std::shared_ptr<RedisClientTuple>> m_connection_redis_client;
};
