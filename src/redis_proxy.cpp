

#include <trantor/utils/Logger.h>

#include "redis_proxy.h"
#include "redis_proxy_timerfd.h"

RedisProxy::RedisProxy(const RedisProxyConfig& redis_proxy_config)
	: m_redis_proxy_config(redis_proxy_config) {
	for (auto& redis_config : m_redis_proxy_config.redis_config_vec) {
		trantor::InetAddress inet_address{redis_config.ip, redis_config.port, redis_config.ipv6};
		m_redis_inet_address.emplace_back(std::move(inet_address));
	}
	m_loop_redis_thread_pool_ptr = std::make_unique<trantor::EventLoopThreadPool>(m_redis_proxy_config.redis_io_thread_num);
	m_loop_redis_thread_pool_ptr->start();
	trantor::InetAddress addr(m_redis_proxy_config.server_address.ip, m_redis_proxy_config.server_address.port, m_redis_proxy_config.server_address.ipv6);
	LOG_INFO << "start create server:" << addr.toIpPort();
	m_tcp_server_ptr = std::make_unique<trantor::TcpServer>(&m_server_event_loop_thread, addr, "redis-proxy-server");
	m_tcp_server_ptr->setRecvMessageCallback([this](const trantor::TcpConnectionPtr& conn_ptr, trantor::MsgBuffer* buffer) {
		onMessage(conn_ptr, buffer);
	});
	m_tcp_server_ptr->setConnectionCallback([this](const trantor::TcpConnectionPtr& conn_ptr) { onConnection(conn_ptr); });
	m_tcp_server_ptr->setIoLoopNum(m_redis_proxy_config.io_thread_num);
	m_tcp_server_ptr->start();
}

RedisProxy::~RedisProxy() {
	LOG_INFO << "start ~RedisProxy!!!";
}

void RedisProxy::quit() {
	LOG_INFO << "start quit RedisProxy!!!";
	m_tcp_server_ptr->stop();
	m_server_event_loop_thread.quit();
}

void RedisProxy::onMessage(const trantor::TcpConnectionPtr& client_conn_ptr, trantor::MsgBuffer* buffer) {
	std::shared_ptr<RedisClientTuple> redis_client_tuple_ptr;
	{
		std::shared_lock<std::shared_mutex> lock(m_mtx);
		if (!m_connection_redis_client.contains(client_conn_ptr)) {
			LOG_ERROR << "connection not exist!!!";
			client_conn_ptr->shutdown();
			return;
		}
		redis_client_tuple_ptr = m_connection_redis_client.at(client_conn_ptr);
	}
	auto& [redis_client_vec, reply_builder_ptr] = *redis_client_tuple_ptr;
	std::string msg{buffer->peek(), buffer->readableBytes()};
	buffer->retrieveAll();
	(*reply_builder_ptr) << msg;
	if (!reply_builder_ptr->replyAvailable())
		return;
	TimerFd time_fd(std::chrono::seconds(m_redis_proxy_config.timeout));
	std::vector<::pollfd> pollfds{::pollfd{time_fd.fd(), POLLIN, 0}};
	std::vector<std::tuple<std::shared_ptr<RedisClient>, bool, bool>> work_redis_client;
	for (auto it = redis_client_vec.begin(); it != redis_client_vec.end();) {
		auto& redis_client_ptr = *it;
		if (redis_client_ptr->disconnected()) {
			LOG_ERROR << "can't connect " << redis_client_ptr->toIpPort() << " start closing now";
			it = redis_client_vec.erase(it);
		}
		else {
			work_redis_client.emplace_back(std::make_tuple(redis_client_ptr, false, true));
			redis_client_ptr->send(reply_builder_ptr->data());
			pollfds.emplace_back(::pollfd{redis_client_ptr->socket(), POLLIN, 0});
			++it;
		}
	}
	reply_builder_ptr->reset();
	if (work_redis_client.empty()) {
		LOG_DEBUG << "all redis client disconnected!!!! ";
		client_conn_ptr->shutdown();
		closeRedisClient(client_conn_ptr);
		return;
	}
	auto work_redis_client_size = work_redis_client.size();
	bool has_send_rsp{false};
	while (true) {
		auto ret = ::poll(pollfds.data(), pollfds.size(), 200);
		if (ret == 0) {
			LOG_DEBUG << "check connection: " << work_redis_client.size();
			size_t disconnected_num = 0;
			for (auto& [redis_client_ptr, recv_rsp, alive] : work_redis_client) {
				if (redis_client_ptr->disconnected()) {
					LOG_DEBUG << "redis client:" << redis_client_ptr->toIpPort() << " disconnected!!!! ";
					recv_rsp = true;
					alive = false;
				}
				if (!alive)
					++disconnected_num;
			}
			if (disconnected_num == work_redis_client_size) {
				LOG_DEBUG << "all redis client disconnected!!!! ";
				client_conn_ptr->shutdown();
				closeRedisClient(client_conn_ptr);
				return;
			}
		}
		if (pollfds[0].revents & POLLIN && time_fd.read()) {
			LOG_ERROR << "timeout!!!";
			size_t disconnected_num{0};
			for (auto& [redis_client_ptr, recv_rsp, alive] : work_redis_client) {
				if (alive && !recv_rsp) {
					LOG_ERROR << "start close redis client";
					redis_client_ptr->stop();
					alive = false;
				}
				if (!alive)
					++disconnected_num;
			}
			if (disconnected_num == work_redis_client_size) {
				LOG_DEBUG << "all redis client timeout!!!! ";
				client_conn_ptr->shutdown();
				closeRedisClient(client_conn_ptr);
				return;
			}
			return;
		}
		for (size_t i = 1; i < pollfds.size(); i++) {
			if (pollfds[i].revents & POLLIN) {
				auto client_seq = i - 1;
				auto& [redis_client_ptr, recv_rsp, alive] = work_redis_client.at(client_seq);
				char c;
				if (::read(redis_client_ptr->socket(), &c, sizeof(c)) == -1) {
					LOG_ERROR << "read msg error, close redis client!!!";
					client_conn_ptr->forceClose();
					closeRedisClient(client_conn_ptr);
					return;
				}
				if (!has_send_rsp) {
					client_conn_ptr->send(redis_client_ptr->data());
					has_send_rsp = true;
				}
				redis_client_ptr->clear();
				recv_rsp = true;
			}
		}
		if (std::all_of(work_redis_client.cbegin(), work_redis_client.cend(), [](const auto& p) {
				auto& [redis_client_ptr, recv_rsp, alive] = p;
				return recv_rsp;
			})) {
			return;
		}
	}
}

void RedisProxy::onConnection(const trantor::TcpConnectionPtr& client_conn_ptr) {
	if (client_conn_ptr->connected()) {
		client_conn_ptr->setTcpNoDelay(true);
		std::vector<std::shared_ptr<RedisClient>> redis_client_vec;
		for (auto& redis_inet_address : m_redis_inet_address) {
			auto redis_client_ptr = std::make_shared<RedisClient>(redis_inet_address, *m_loop_redis_thread_pool_ptr);
			if (redis_client_ptr->start())
				redis_client_vec.emplace_back(std::move(redis_client_ptr));
		}
		if (redis_client_vec.empty()) {
			LOG_ERROR << "can't create connect to all redis";
			return;
		}
		auto tp = std::make_shared<RedisClientTuple>(std::move(redis_client_vec), std::make_unique<redis_reply::ReplyBuilder>(true));
		std::unique_lock<std::shared_mutex> lk(m_mtx);
		m_connection_redis_client.emplace(client_conn_ptr, std::move(tp));
	}
	else if (client_conn_ptr->disconnected()) {
		LOG_INFO << "connection disconnected";
		closeRedisClient(client_conn_ptr);
	}
}

void RedisProxy::closeRedisClient(const trantor::TcpConnectionPtr& client_conn_ptr) {
	LOG_INFO << "call closeRedisClient!!!";
	std::unique_lock<std::shared_mutex> lk(m_mtx);
	if (!m_connection_redis_client.contains(client_conn_ptr))
		return;
	auto& [redis_client_vec, reply_builder_ptr] = *m_connection_redis_client[client_conn_ptr];
	for (auto& redis_client_ptr : redis_client_vec)
		redis_client_ptr->stop();
	m_connection_redis_client.erase(client_conn_ptr);
}

void RedisProxy::run() {
	m_server_event_loop_thread.loop();
}
