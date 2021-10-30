#pragma once

#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#define REDIS_REPLY_ERR 0
#define REDIS_REPLY_BULK 1
#define REDIS_REPLY_SIMPLE 2
#define REDIS_REPLY_NULL 3
#define REDIS_REPLY_INT 4
#define REDIS_REPLY_ARRAY 5

namespace redis_reply {

class Reply;

class BuilderIface {
public:
	virtual ~BuilderIface() = default;
	virtual BuilderIface& operator<<(std::string& data) = 0;
	virtual bool reply_ready() const = 0;
	virtual Reply& get_reply() = 0;
};

std::unique_ptr<BuilderIface> createBuilder(char id);

class Reply {
public:
	enum class type {
		error = REDIS_REPLY_ERR,
		bulk_string = REDIS_REPLY_BULK,
		simple_string = REDIS_REPLY_SIMPLE,
		null = REDIS_REPLY_NULL,
		integer = REDIS_REPLY_INT,
		array = REDIS_REPLY_ARRAY
	};

	enum class string_type {
		error = REDIS_REPLY_ERR,
		bulk_string = REDIS_REPLY_BULK,
		simple_string = REDIS_REPLY_SIMPLE
	};

public:
	Reply()
		: m_type(type::null) {}

	Reply(const std::string& value, string_type reply_type)
		: m_type(static_cast<type>(reply_type))
		, m_str_val(value) {
	}

	explicit Reply(int64_t value)
		: m_type(type::integer)
		, m_int_val(value) {
	}

	explicit Reply(const std::vector<Reply>& rows)
		: m_type(type::array)
		, m_rows(rows) {
	}

	~Reply() = default;

	Reply(Reply&& other) noexcept {
		m_type = other.m_type;
		m_rows = std::move(other.m_rows);
		m_str_val = std::move(other.m_str_val);
		m_int_val = other.m_int_val;
	}

	Reply(const Reply&) = default;
	Reply& operator=(const Reply&) = default;

	Reply& operator=(Reply&& other) noexcept {
		if (this != &other) {
			m_type = other.m_type;
			m_rows = std::move(other.m_rows);
			m_str_val = std::move(other.m_str_val);
			m_int_val = other.m_int_val;
		}
		return *this;
	}

	bool ok() const {
		return !is_error();
	}

	bool ko() const {
		return !ok();
	}

	const std::string& error() const {
		if (!is_error())
			throw std::runtime_error("Reply is not an error");
		return as_string();
	}

	explicit operator bool() const {
		return !is_error() && !is_null();
	}

	void set() {
		m_type = type::null;
	}

	void set(const std::string& value, string_type reply_type) {
		m_type = static_cast<type>(reply_type);
		m_str_val = value;
	}

	void set(int64_t value) {
		m_type = type::integer;
		m_int_val = value;
	}

	void set(const std::vector<Reply>& rows) {
		m_type = type::array;
		m_rows = rows;
	}

	Reply& operator<<(const Reply& reply) {
		m_type = type::array;
		m_rows.push_back(reply);
		return *this;
	}

	bool is_array() const {
		return m_type == type::array;
	}

	bool is_string() const {
		return is_simple_string() || is_bulk_string() || is_error();
	}

	bool is_simple_string() const {
		return m_type == type::simple_string;
	}

	bool is_bulk_string() const {
		return m_type == type::bulk_string;
	}

	bool is_error() const {
		return m_type == type::error;
	}

	bool is_integer() const {
		return m_type == type::integer;
	}

	bool is_null() const {
		return m_type == type::null;
	}

	const std::vector<Reply>& as_array() const {
		if (!is_array())
			throw std::runtime_error("Reply is not an array");
		return m_rows;
	}

	const std::string& as_string() const {
		if (!is_string())
			throw std::runtime_error("Reply is not a string");
		return m_str_val;
	}

	int64_t as_integer() const {
		if (!is_integer())
			throw std::runtime_error("Reply is not an integer");
		return m_int_val;
	}

	Reply::type get_type() const {
		return m_type;
	}

	std::optional<int64_t> try_get_int() const {
		if (is_integer())
			return std::optional<int64_t>(m_int_val);
		return {};
	}

private:
	type m_type;
	std::vector<Reply> m_rows;
	std::string m_str_val;
	int64_t m_int_val;
};

class SimpleStringBuilder : public BuilderIface {
public:
	SimpleStringBuilder()
		: m_str("")
		, m_reply_ready(false) {
	}
	~SimpleStringBuilder() override = default;

	SimpleStringBuilder(const SimpleStringBuilder&) = delete;
	SimpleStringBuilder& operator=(const SimpleStringBuilder&) = delete;

	BuilderIface& operator<<(std::string& buffer) override {
		if (m_reply_ready)
			return *this;
		auto end_sequence = buffer.find("\r\n");
		if (end_sequence == std::string::npos)
			return *this;
		m_str = buffer.substr(0, end_sequence);
		m_reply.set(m_str, Reply::string_type::simple_string);
		buffer.erase(0, end_sequence + 2);
		m_reply_ready = true;
		return *this;
	}

	virtual bool reply_ready() const override {
		return m_reply_ready;
	}

	Reply& get_reply() {
		return m_reply;
	}

	virtual const std::string& get_simple_string() const {
		return m_str;
	}

private:
	std::string m_str;
	bool m_reply_ready;
	Reply m_reply;
};

class ErrorBuilder : public BuilderIface {
public:
	ErrorBuilder() = default;
	~ErrorBuilder() override = default;

	ErrorBuilder(const ErrorBuilder&) = delete;
	ErrorBuilder& operator=(const ErrorBuilder&) = delete;

	virtual BuilderIface& operator<<(std::string& buffer) override {
		m_string_builder << buffer;

		if (m_string_builder.reply_ready())
			m_reply.set(m_string_builder.get_simple_string(), Reply::string_type::error);

		return *this;
	}

	virtual bool reply_ready() const override {
		return m_string_builder.reply_ready();
	}

	Reply& get_reply() {
		return m_reply;
	}

	const std::string& get_error() const {
		return m_string_builder.get_simple_string();
	}

private:
	SimpleStringBuilder m_string_builder;
	Reply m_reply;
};

class IntegerBuilder : public BuilderIface {
public:
	IntegerBuilder()
		: m_nbr(0)
		, m_negative_multiplicator(1)
		, m_reply_ready(false) {
	}

	~IntegerBuilder() override = default;
	IntegerBuilder(const IntegerBuilder&) = delete;
	IntegerBuilder& operator=(const IntegerBuilder&) = delete;

	virtual BuilderIface& operator<<(std::string& buffer) override {
		if (m_reply_ready)
			return *this;
		auto end_sequence = buffer.find("\r\n");
		if (end_sequence == std::string::npos)
			return *this;
		std::size_t i;
		for (i = 0; i < end_sequence; i++) {
			if (!i && m_negative_multiplicator == 1 && buffer[i] == '-') {
				m_negative_multiplicator = -1;
				continue;
			}
			else if (!std::isdigit(buffer[i]))
				throw std::runtime_error("Invalid character for integer redis reply");
			m_nbr *= 10;
			m_nbr += buffer[i] - '0';
		}
		buffer.erase(0, end_sequence + 2);
		m_reply.set(m_negative_multiplicator * m_nbr);
		m_reply_ready = true;
		return *this;
	}

	virtual bool reply_ready() const override {
		return m_reply_ready;
	}

	Reply& get_reply() {
		return m_reply;
	}

	int64_t get_integer() const {
		return m_negative_multiplicator * m_nbr;
	}

private:
	int64_t m_nbr;
	int64_t m_negative_multiplicator;
	bool m_reply_ready;
	Reply m_reply;
};

class BulkStringBuilder : public BuilderIface {
public:
	BulkStringBuilder()
		: m_str_size(0)
		, m_str("")
		, m_is_null(false)
		, m_reply_ready(false) {
	}
	~BulkStringBuilder() override = default;

	BulkStringBuilder(const BulkStringBuilder&) = delete;
	BulkStringBuilder& operator=(const BulkStringBuilder&) = delete;

	virtual BuilderIface& operator<<(std::string& buffer) override {
		if (m_reply_ready)
			return *this;
		if (!fetch_size(buffer) || m_reply_ready)
			return *this;
		fetch_str(buffer);
		return *this;
	}

	virtual bool reply_ready() const override {
		return m_reply_ready;
	}

	Reply& get_reply() {
		return m_reply;
	}

	const std::string& get_bulk_string() const {
		return m_str;
	}

	bool is_null() const {
		return m_is_null;
	}

private:
	void build_reply() {
		if (m_is_null)
			m_reply.set();
		else
			m_reply.set(m_str, Reply::string_type::bulk_string);
		m_reply_ready = true;
	}

	bool fetch_size(std::string& buffer) {
		if (m_int_builder.reply_ready())
			return true;
		m_int_builder << buffer;
		if (!m_int_builder.reply_ready())
			return false;
		m_str_size = (int)m_int_builder.get_integer();
		if (m_str_size == -1) {
			m_is_null = true;
			build_reply();
		}
		return true;
	}

	void fetch_str(std::string& buffer) {
		if (buffer.size() < static_cast<std::size_t>(m_str_size) + 2)
			return;
		if (buffer[m_str_size] != '\r' || buffer[m_str_size + 1] != '\n')
			throw std::runtime_error("Wrong ending sequence");
		m_str = buffer.substr(0, m_str_size);
		buffer.erase(0, m_str_size + 2);
		build_reply();
	}

	IntegerBuilder m_int_builder;
	int m_str_size;
	std::string m_str;
	bool m_is_null;
	bool m_reply_ready;
	Reply m_reply;
};

class ArrayBuilder : public BuilderIface {
public:
	ArrayBuilder()
		: m_current_builder(nullptr)
		, m_reply_ready(false)
		, m_reply(std::vector<Reply>{}) {
	}
	~ArrayBuilder() override = default;

	ArrayBuilder(const ArrayBuilder&) = delete;
	ArrayBuilder& operator=(const ArrayBuilder&) = delete;

	BuilderIface& operator<<(std::string& buffer) {
		if (m_reply_ready)
			return *this;
		if (!fetch_array_size(buffer))
			return *this;
		while (buffer.size() && !m_reply_ready)
			if (!build_row(buffer))
				return *this;
		return *this;
	}

	virtual bool reply_ready() const override {
		return m_reply_ready;
	}

	Reply& get_reply() {
		return m_reply;
	}

private:
	bool fetch_array_size(std::string& buffer) {
		if (m_int_builder.reply_ready())
			return true;
		m_int_builder << buffer;
		if (!m_int_builder.reply_ready())
			return false;
		int64_t size = m_int_builder.get_integer();
		if (size < 0) {
			m_reply.set();
			m_reply_ready = true;
		}
		else if (size == 0) {
			m_reply_ready = true;
		}
		m_array_size = size;
		return true;
	};

	bool build_row(std::string& buffer) {
		if (!m_current_builder) {
			m_current_builder = createBuilder(buffer.front());
			buffer.erase(0, 1);
		}
		*m_current_builder << buffer;
		if (!m_current_builder->reply_ready())
			return false;
		m_reply << m_current_builder->get_reply();
		m_current_builder = nullptr;
		if (m_reply.as_array().size() == m_array_size)
			m_reply_ready = true;
		return true;
	}

	IntegerBuilder m_int_builder;
	uint64_t m_array_size;
	std::unique_ptr<BuilderIface> m_current_builder;
	bool m_reply_ready;
	Reply m_reply;
};

inline std::unique_ptr<BuilderIface> createBuilder(char id) {
	switch (id) {
	case '+':
		return std::make_unique<SimpleStringBuilder>();
	case '-':
		return std::make_unique<ErrorBuilder>();
	case ':':
		return std::make_unique<IntegerBuilder>();
	case '$':
		return std::make_unique<BulkStringBuilder>();
	case '*':
		return std::make_unique<ArrayBuilder>();
	default:
		throw std::runtime_error("Invalid data");
	}
}

class ReplyBuilder {
public:
	ReplyBuilder(bool flag = false)
		: m_builder(nullptr)
		, m_flag(flag) {
	}
	~ReplyBuilder() = default;

	ReplyBuilder(const ReplyBuilder&) = delete;
	ReplyBuilder& operator=(const ReplyBuilder&) = delete;

	ReplyBuilder& operator<<(const std::string& data) {
		m_buffer += data;
		m_old_buffer += data;
		while (build_reply()) {
		}
		return *this;
	}

	ReplyBuilder& operator<<(const char* data) {
		m_buffer += data;
		m_old_buffer += data;
		while (build_reply()) {
		}
		return *this;
	}

	void reset() {
		m_builder = nullptr;
		m_buffer.clear();
		m_old_buffer.clear();
		m_available_replies.clear();
	}

	auto& data() {
		return m_old_buffer;
	}

	bool replyAvailable() const {
		return !m_available_replies.empty();
	}

private:
	bool build_reply() {
		if (m_buffer.empty())
			return false;
		if (!m_builder) {
			if (m_flag && m_buffer.front() != '*')
				m_builder = createBuilder('+');
			else {
				m_builder = createBuilder(m_buffer.front());
				m_buffer.erase(0, 1);
			}
		}
		*m_builder << m_buffer;
		if (m_builder->reply_ready()) {
			m_available_replies.push_back(m_builder->get_reply());
			m_builder = nullptr;
			return true;
		}
		return false;
	}

	void operator>>(Reply& reply) {
		reply = get_front();
	}

	const Reply& get_front() const {
		if (!replyAvailable())
			throw std::runtime_error("No available reply");
		return m_available_replies.front();
	}

	void pop_front() {
		if (!replyAvailable())
			throw std::runtime_error("No available reply");
		m_available_replies.pop_front();
	}

	std::string m_buffer;
	std::string m_old_buffer;
	std::unique_ptr<BuilderIface> m_builder;
	std::deque<Reply> m_available_replies;
	bool m_flag;
};

} // namespace redis_reply