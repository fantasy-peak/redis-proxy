#pragma once

#include <fcntl.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <chrono>

class TimerFd {
public:
	TimerFd(std::chrono::high_resolution_clock::duration duration) {
		m_fd = ::timerfd_create(CLOCK_MONOTONIC, 0);
		if (m_fd == -1)
			throw std::system_error(errno, std::generic_category(), "can not create timerfd");
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
		auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
		::timespec interval{static_cast<time_t>(seconds.count()), static_cast<long>(nanoseconds.count())};
		::timespec now;
		if (::clock_gettime(CLOCK_MONOTONIC, &now) == -1)
			throw std::system_error(errno, std::generic_category(), "can not get current time");
		now.tv_sec += interval.tv_sec;
		now.tv_nsec += interval.tv_nsec;
		if (now.tv_nsec > 1000000000l) {
			now.tv_sec += interval.tv_nsec / 1000000000l;
			now.tv_nsec %= 1000000000l;
		}
		auto first_expired = now;
		::itimerspec new_value{interval, first_expired};
		if (::timerfd_settime(m_fd, TFD_TIMER_ABSTIME, &new_value, nullptr) == -1)
			throw std::system_error(errno, std::generic_category(), "timerfd_settime error");
		auto flags = ::fcntl(m_fd, F_GETFL, 0);
		if (flags == -1)
			throw std::system_error(errno, std::generic_category(), "can not get descriptor flags");
		if (::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) == -1)
			throw std::system_error(errno, std::generic_category(), "can not set descriptor flags");
	}

	~TimerFd() {
		close(m_fd);
	}

	int fd() const {
		return m_fd;
	}

	std::optional<uint64_t> read() {
		uint64_t expired;
		auto res = ::read(m_fd, &expired, sizeof(expired));
		if (res == -1)
			return {};
		return expired;
	}

public:
	int m_fd;
};
