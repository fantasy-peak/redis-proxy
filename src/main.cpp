#include <cstring>
#include <iostream>
#include <optional>
#include <thread>

#include <spdlog/spdlog.h>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/experimental/as_tuple.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>

#include "redis_proxy_config.h"
#include "redis_reply.h"

constexpr int reply_buffer_size = 1024 * 6;

consteval int get_reply_buffer_size() {
	return reply_buffer_size - 1;
}

class RedisClient final {
public:
	RedisClient(const std::shared_ptr<asio::ip::tcp::endpoint>& endpoint_ptr)
		: m_endpoint_ptr(endpoint_ptr)
		, m_reply_str_ptr(std::make_unique<char[]>(reply_buffer_size)) {
		std::memset(m_reply_str_ptr.get(), 0x00, reply_buffer_size);
	}

	asio::awaitable<bool> async_connect() {
		auto executor = co_await asio::this_coro::executor;
		m_socket_ptr = std::make_shared<asio::ip::tcp::socket>(executor);
		asio::error_code ec{};
		co_await m_socket_ptr->async_connect(*m_endpoint_ptr, asio::redirect_error(asio::use_awaitable, ec));
		if (ec) {
			spdlog::error("[RedisClient::async_connect] connect redis [{}] error.", to_ip_port());
			co_return false;
		}
		co_return true;
	}

	asio::awaitable<int> async_write_some(const std::string& data) {
		asio::error_code ec{};
		auto count = co_await m_socket_ptr->async_write_some(asio::buffer(&data[0], data.size()), asio::redirect_error(asio::use_awaitable, ec));
		if (ec) {
			spdlog::error("[RedisClient::async_connect] async_write_some redis [{}:{}] error.", to_ip_port());
			co_return -1;
		}
		co_return count;
	}

	asio::awaitable<std::optional<std::string>> async_read_some() {
		m_reply_builder.reset();
		std::memset(m_reply_str_ptr.get(), 0x00, reply_buffer_size);
		for (;;) {
			// https://github.com/chriskohlhoff/asio/issues/915
			asio::error_code ec{};
			co_await m_socket_ptr->async_read_some(asio::buffer(m_reply_str_ptr.get(), get_reply_buffer_size()), asio::redirect_error(asio::use_awaitable, ec));
			if (ec) {
				spdlog::error("[RedisClient::async_connect] async_read_some redis [{}:{}] error.", to_ip_port());
				co_return std::nullopt;
			}
			m_reply_builder << m_reply_str_ptr.get();
			if (m_reply_builder.replyAvailable())
				co_return std::make_optional(std::move(m_reply_builder.data()));
			std::memset(m_reply_str_ptr.get(), 0x00, reply_buffer_size);
		}
		co_return std::nullopt;
	}

	auto is_open() {
		return m_socket_ptr->is_open();
	}

private:
	std::string to_ip_port() {
		return m_endpoint_ptr->address().to_string() + ":" + std::to_string(m_endpoint_ptr->port());
	}

	std::shared_ptr<asio::ip::tcp::socket> m_socket_ptr;
	std::shared_ptr<asio::ip::tcp::endpoint> m_endpoint_ptr;
	redis_reply::ReplyBuilder m_reply_builder;
	std::unique_ptr<char[]> m_reply_str_ptr;
};

class RedisProxy final {
public:
	RedisProxy(const RedisProxyConfig& redis_proxy_config)
		: m_config(redis_proxy_config)
		, m_io_context_ptr(std::make_unique<asio::io_context>(redis_proxy_config.io_thread_num)) {
		m_timeout_loop_num = m_config.timeout * 1000 * 1000 / m_config.timeout_loop_interval;
		spdlog::info("[RedisProxy::RedisProxy] m_timeout_loop_num: {}", m_timeout_loop_num);
		for (auto& redis_config : redis_proxy_config.redis_config_vec) {
			spdlog::info("[RedisProxy::RedisProxy] create endpoint: [{}:{}]", redis_config.ip, redis_config.port);
			auto endpoint_ptr = std::make_shared<asio::ip::tcp::endpoint>(asio::ip::address::from_string(redis_config.ip), redis_config.port);
			m_endpoint_ptrs.emplace_back(std::move(endpoint_ptr));
		}
	}

	void start() {
		asio::co_spawn(*m_io_context_ptr, listener(), asio::detached);
		for (int i = 0; i < m_config.io_thread_num; i++)
			m_threads.emplace_back(std::thread([&] { m_io_context_ptr->run(); }));
		std::ranges::for_each(m_threads, [](auto& p) { p.join(); });
	}

	void stop() {
		m_io_context_ptr->stop();
	}

	auto& context() {
		return *m_io_context_ptr;
	}

private:
	asio::awaitable<std::vector<std::shared_ptr<RedisClient>>> create_client() {
		std::vector<std::shared_ptr<RedisClient>> redis_clients;
		for (auto& endpoint_ptr : m_endpoint_ptrs) {
			auto tcp_client_ptr = std::make_shared<RedisClient>(endpoint_ptr);
			auto ret = co_await tcp_client_ptr->async_connect();
			if (!ret)
				continue;
			redis_clients.emplace_back(std::move(tcp_client_ptr));
		}
		co_return redis_clients;
	}

	asio::awaitable<void> process_request(asio::ip::tcp::socket socket) {
		auto redis_clients = co_await create_client();
		if (redis_clients.empty()) {
			spdlog::error("[RedisProxy::process_request] create redis client error, so close socket.");
			socket.close();
			co_return;
		}
		auto reply_builder_ptr = std::make_unique<redis_reply::ReplyBuilder>(true);
		constexpr int buffer_size = 4096;
		auto buffer_ptr = std::make_unique<char[]>(buffer_size);
		auto get_buffer_max_size = []() consteval {
			return buffer_size - 1;
		};
		for (;;) {
			std::erase_if(redis_clients, [](auto& redis_client_ptr) { return !redis_client_ptr->is_open(); });
			if (redis_clients.empty()) {
				spdlog::error("[RedisProxy::process_request] all client close.");
				socket.close();
				co_return;
			}
			std::memset(buffer_ptr.get(), 0x00, buffer_size);
			// https://github.com/chriskohlhoff/asio/issues/915
			[[maybe_unused]] auto [ec, n] = co_await socket.async_read_some(asio::buffer(buffer_ptr.get(), get_buffer_max_size()),
				asio::experimental::as_tuple(asio::use_awaitable));
			if (ec) {
				spdlog::error("[RedisProxy::process_request] async_read_some [{}]", ec.message());
				socket.close();
				co_return;
			}
			(*reply_builder_ptr) << buffer_ptr.get();
			if (!reply_builder_ptr->replyAvailable())
				continue;
			auto forward_data_ptr = std::make_shared<std::string>(std::move(reply_builder_ptr->data()));
			reply_builder_ptr->reset();

			// 0 Pending, 1 Done, 2 Error
			auto dispatch = [](std::shared_ptr<std::string> forward_data_ptr,
								std::shared_ptr<std::tuple<std::atomic<int16_t>, std::string, std::shared_ptr<RedisClient>>> done) -> asio::awaitable<void> {
				auto& [state, redis_rsp_str, tcp_client_ptr] = *done;
				auto send_ret = co_await tcp_client_ptr->async_write_some(*forward_data_ptr);
				if (send_ret == -1) {
					state = 2;
					co_return;
				}
				auto recv_opt = co_await tcp_client_ptr->async_read_some();
				if (!recv_opt.has_value()) {
					state = 2;
					co_return;
				}
				redis_rsp_str = std::move(recv_opt.value());
				state = 1;
			};

			std::vector<std::shared_ptr<std::tuple<std::atomic<int16_t>, std::string, std::shared_ptr<RedisClient>>>> dones;
			auto executor = co_await asio::this_coro::executor;
			for (auto& redis_client_ptr : redis_clients) {
				auto done = std::make_shared<std::tuple<std::atomic<int16_t>, std::string, std::shared_ptr<RedisClient>>>(0, "", redis_client_ptr);
				asio::co_spawn(executor, dispatch(forward_data_ptr, dones.emplace_back(std::move(done))), asio::detached);
			}
			asio::steady_timer timer(executor);
			timer.expires_after(asio::chrono::microseconds(m_config.timeout_loop_interval));
			auto has_send_rsp = std::make_shared<std::atomic_bool>(false);
			for (uint32_t i = 0; i < m_timeout_loop_num; i++) {
				std::error_code ec{};
				co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
				if (ec) {
					spdlog::error("[RedisProxy::process_request] timer error [{}]", ec.message());
					socket.close();
					co_return;
				}
				// 0 Pending, 1 Done, 2 Error
				std::erase_if(dones, [](auto& ptr) {
					auto& [state, str, redis_client_ptr] = *ptr;
					return state == 2;
				});
				if (dones.empty()) {
					spdlog::error("[RedisProxy::process_request] dones empty!!! [{}]", ec.message());
					socket.close();
					co_return;
				}
				auto async_write_some = [&](const std::string& str) -> asio::awaitable<void> {
					try {
						if (*has_send_rsp)
							co_return;
						*has_send_rsp = true;
						co_await socket.async_write_some(asio::buffer(str.c_str(), str.size()), asio::use_awaitable);
					} catch (const std::exception&) {
						std::cout << "响应客户端失败" << std::endl;
					}
					co_return;
				};
				// 全部成功
				if (std::ranges::all_of(dones, [](auto& p) { return std::get<0>(*p) == 1; })) {
					auto& [state, str, redis_client_ptr] = *dones[0];
					co_await async_write_some(str);
					break;
				}
				// 一个成功 一个 pengding
				if (std::ranges::any_of(dones, [](auto& p) { return std::get<0>(*p) == 1; })) {
					for (auto& ptr : dones) {
						auto& [state, str, redis_client_ptr] = *ptr;
						if (state == 1) {
							co_await async_write_some(str);
							break;
						}
					}
				}
				timer.expires_at(timer.expiry() + asio::chrono::microseconds(m_config.timeout_loop_interval));
			}
			for (auto& ptr : dones) {
				auto& [state, str, client_ptr] = *ptr;
				if (state == 0)
					std::erase_if(redis_clients, [&](auto& redis_client_ptr) { return redis_client_ptr == client_ptr; });
			}
		}
	}

	asio::awaitable<void> listener() {
		auto executor = co_await asio::this_coro::executor;
		asio::ip::tcp::acceptor acceptor(executor, {asio::ip::address::from_string(m_config.endpoint.ip), m_config.endpoint.port});
		for (;;) {
			asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable_t(__FILE__, __LINE__, __PRETTY_FUNCTION__));
			asio::co_spawn(executor, process_request(std::move(socket)), asio::detached);
		}
	}

	RedisProxyConfig m_config;
	std::shared_ptr<asio::io_context> m_io_context_ptr;
	std::vector<std::thread> m_threads;
	std::vector<std::shared_ptr<asio::ip::tcp::endpoint>> m_endpoint_ptrs;
	uint32_t m_timeout_loop_num;
};

int main(int, char** argv) {
	try {
		auto redis_proxy_config_ptr = parse_file(argv[1]);
		RedisProxy redis_proxy{*redis_proxy_config_ptr};
		redis_proxy_config_ptr.reset();
		asio::signal_set signals(redis_proxy.context(), SIGINT, SIGTERM);
		signals.async_wait([&](auto, auto) { redis_proxy.stop(); });
		redis_proxy.start();
		return 0;
	} catch (std::exception& e) {
		spdlog::info("[RedisProxy] Exception: {}", e.what());
		return -1;
	}
}
