/*
 * Copyright (C) 2023  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IBMULATOR_NETSERVICE_H
#define IBMULATOR_NETSERVICE_H

#include "shared_fifo.h"
#include "ring_buffer.h"
#include <atomic>
#include <thread>
#include <condition_variable>

#ifdef _WIN32
	#include "wincompat.h"
	#include <winsock2.h>
#else
	typedef int SOCKET;
	#define INVALID_SOCKET -1
#endif

#define DEFAULT_TX_FIFO_SIZE 1024
#define DEFAULT_RX_FIFO_SIZE 1024
#define MIN_RX_FIFO_SIZE 16
	
class NetService
{
public:
	enum class Mode {
		Client, ClientAsync, Server
	};

	class TXFifo : public RingBuffer {
	protected:
		std::atomic<unsigned> m_threshold = 1;
		std::condition_variable m_data_cond;
	public:
		virtual size_t read(uint8_t *_data, size_t _len, uint64_t _max_wait_ns);
		virtual size_t write(uint8_t *_data, size_t _len);
		static unsigned ms_to_bytes(double _ms, unsigned _bps) {
			return _ms * double(_bps) / 10000.0;
		}
		static unsigned bytes_to_ms(unsigned _bytes, unsigned _bps) {
			return double(_bytes) / double(_bps) * 10000.0;
		}
		void set_threshold(unsigned bytes) {
			m_threshold = std::max(bytes, 1u);
		}
		unsigned get_threshold() const { return m_threshold; }
	};

	using RXFifo = SharedFifo<uint8_t>;

	enum class Error {
		NoError,
		Listen,
		Connect,
		NoRoute,
		HostDown,
		HostRefused,
		Aborted,
		Hostname,
		Socket,
		Terminated
	};
	
protected:
	std::string m_log_name;

	std::string m_server_host;
	unsigned m_server_port = 0;
	std::string m_client_host;
	unsigned m_client_port = 0;
	std::string m_client_name;
	std::atomic<SOCKET> m_server_socket = INVALID_SOCKET;
	std::atomic<SOCKET> m_client_socket = INVALID_SOCKET;
	std::atomic<bool> m_server_accepted = false;
	std::mutex m_conn_mutex;
	std::condition_variable m_conn_cv;
	std::atomic<bool> m_abort = false;
	std::atomic<Error> m_error = Error::NoError;
	std::atomic<bool> m_server_refuse = false;
	std::thread m_server_thread;
	std::thread m_client_thread;
	RXFifo m_rx_data;
	TXFifo m_tx_data;
	double m_tx_delay_ms = 0.0;
	bool m_tcp_nodelay = true;
	bool m_rx_overflow = true;

	std::function<void(std::string)> m_mex_callback = nullptr;
	std::atomic<double> m_cycles_factor = 1.0;

public:
	NetService();

	void set_log_name(const char *_name) { m_log_name = _name; }
	void set_mex_callback(std::function<void(std::string)> _fn) { m_mex_callback = _fn; }

	static std::pair<std::string,uint16_t> parse_address(std::string _address, uint16_t _default_port);
	void open(const char *_host, uint16_t _port, Mode _mode, uint64_t _conn_timeout_ms);
	void close();
	void close_client(bool _refuse = false);

	void set_tcp_nodelay(bool _value) { m_tcp_nodelay = _value; }
	void set_rx_queue(size_t _fifo_size, bool _oveflow);
	void set_tx_queue(size_t _fifo_size);
	void set_tx_threshold(double _delay_ms, unsigned _bitrate);
	void cycles_adjust(double _factor) { m_cycles_factor = _factor; }
	bool is_connected() const { return m_client_socket != INVALID_SOCKET; }
	bool is_server_ready() const { return m_server_socket != INVALID_SOCKET && !m_server_accepted; }
	bool has_server_accepted() const { return m_server_accepted && is_connected(); }
	void set_server_not_ready() { m_server_refuse = true; }
	void set_server_ready() { m_server_refuse = false; }
	bool is_rx_active() const { return !(m_rx_data.was_empty()); }
	bool is_tx_active() const { return (m_tx_data.get_read_avail()); }
	void abort_connection() {
		std::lock_guard lock(m_conn_mutex);
		m_abort = true;
		m_conn_cv.notify_all();
	}
	Error get_error() const { return m_error; }
	void clear_error() { m_error = Error::NoError; }
	void clear_queues();

	RXFifo & rx_fifo() { return m_rx_data; }
	TXFifo & tx_fifo() { return m_tx_data; }

protected:
	const char *log_name() const { return m_log_name.c_str(); }

	SOCKET create_socket(const char *_host, uint16_t _port, struct sockaddr_in &sin_);
	void start_net_server();
	void start_net_client();
	void start_net_client_async(uint64_t _conn_timeout_ms);
	void net_data_loop();
	void net_tx_loop();
	void close_client_socket(Error, bool _refuse = false);
};

#endif