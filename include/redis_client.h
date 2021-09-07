#pragma once

#include <unistd.h>

#include <atomic>

#include <trantor/net/EventLoop.h>
#include <trantor/net/TcpClient.h>

#include "redis_reply.h"

class RedisClient : public std::enable_shared_from_this<RedisClient> {
public:
	RedisClient(const trantor::InetAddress& redis_address, trantor::EventLoop* loop)
		: m_redis_address(redis_address)
		, m_event_loop_ptr(loop) {
		m_tcp_client = std::make_unique<trantor::TcpClient>(m_event_loop_ptr, redis_address, "tcpclienttest");
		if (::pipe(m_pipe_fd) == -1)
			throw std::system_error(errno, std::system_category(), "failure in RedisClient constructor");
	}

	~RedisClient() {
		close(m_pipe_fd[0]);
		close(m_pipe_fd[1]);
	}

	bool start() {
		auto connect_result = std::make_shared<std::atomic<int16_t>>(0);
		std::weak_ptr ptr = shared_from_this();
		m_tcp_client->setConnectionCallback([this, ptr, connect_result](const trantor::TcpConnectionPtr& conn_ptr) {
			if (auto spt = ptr.lock()) {
				*connect_result = 1;
				if (conn_ptr->connected()) {
					m_redis_connection_ptr = conn_ptr;
					m_connected = true;
					LOG_INFO << "address: " << m_redis_address.toIpPort() << " connect!!!";
				}
				else {
					LOG_INFO << "address: " << m_redis_address.toIpPort() << " Disconnect!!!";
					m_connected = false;
				}
			}
		});
		m_tcp_client->setMessageCallback([this, ptr](const trantor::TcpConnectionPtr&, trantor::MsgBuffer* buffer) {
			if (auto spt = ptr.lock()) {
				m_reply_builder << buffer->read(buffer->readableBytes());
				if (m_reply_builder.replyAvailable()) {
					const char c = '\0';
					write(m_pipe_fd[1], &c, sizeof(c));
				}
			}
		});
		m_tcp_client->setConnectionErrorCallback([this, ptr, connect_result] {
			*connect_result = 2;
			if (auto spt = ptr.lock())
				LOG_ERROR << "address: " << m_redis_address.toIpPort() << " connect error!!!";
		});
		m_tcp_client->connect();
		while (true) {
			if (*connect_result != 0)
				break;
		}
		return *connect_result != 2;
	}

	void disconnect() {
		m_tcp_client->disconnect();
	}

	template <typename T>
	void send(T&& buffer) {
		m_redis_connection_ptr->send(std::forward<T>(buffer));
	}

	auto& socket() {
		return m_pipe_fd[0];
	}

	bool connected() const {
		return m_connected;
	}

	bool disconnected() {
		return !m_connected;
	}

	void clear() {
		m_reply_builder.reset();
	}

	auto& data() {
		return m_reply_builder.data();
	}

	std::string toIpPort() {
		return m_redis_address.toIpPort();
	}

	trantor::EventLoop* getLoop() {
		return m_event_loop_ptr;
	}

private:
	trantor::InetAddress m_redis_address;
	std::unique_ptr<trantor::TcpClient> m_tcp_client;
	trantor::EventLoop* m_event_loop_ptr{nullptr};
	trantor::TcpConnectionPtr m_redis_connection_ptr{nullptr};
	redis_reply::ReplyBuilder m_reply_builder;
	int m_pipe_fd[2];
	std::atomic_bool m_connected{false};
};
