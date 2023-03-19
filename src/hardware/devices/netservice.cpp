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

#include "ibmulator.h"
#include "netservice.h"
#include "utils.h"
#include "timers.h"
#include "chrono.h"

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define WOULD_BLOCK WSAEWOULDBLOCK
	#define IN_PROGRESS WSAEINPROGRESS
	inline static int get_neterr() {
		return WSAGetLastError();
	}
	inline static std::string get_neterr_str(int _error) {
		wchar_t *s = NULL;
		FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
		               NULL, _error,
		               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		               (LPWSTR)&s, 0, NULL);
		std::string message = utf8::narrow(s);
		LocalFree(s);
		return message;
	}
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/tcp.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <poll.h>
	#define closesocket(s) ::close(s)
	#define SOCKET_ERROR -1
	#define SD_BOTH SHUT_RDWR
	#define WOULD_BLOCK EWOULDBLOCK
	#define IN_PROGRESS EINPROGRESS
	inline static int get_neterr() {
		return errno;
	}
	inline static std::string get_neterr_str(int _error) {
		return strerror(_error);
	}
#endif
#include <fcntl.h>

#define SEND_MAX_DELAY_MS 100
#define SEND_MAX_DELAY_NS 100_ms



size_t NetService::TXFifo::read(uint8_t *_data, size_t _len, uint64_t _max_wait_ns)
{
	// consumer (the tx thread use this and awaits on threshold)
	if(get_read_avail() < m_threshold) {
		std::unique_lock<std::mutex> lock(m_mutex);
		m_data_cond.wait_for(lock, std::chrono::nanoseconds(_max_wait_ns));
	}
	return RingBuffer::read(_data, _len);
}


size_t NetService::TXFifo::write(uint8_t *_data, size_t _len)
{
	// producer (the machine use this, the tx thread awaits on threshold)
	size_t len = RingBuffer::write(_data, _len);
	if(get_read_avail() >= m_threshold) {
		m_data_cond.notify_one();
	}
	return len;
}

NetService::NetService()
{
	m_mex_callback = [](std::string){};

	m_rx_data.set_max_size(DEFAULT_RX_FIFO_SIZE);
	m_tx_data.set_size(DEFAULT_TX_FIFO_SIZE);
}

SOCKET NetService::create_socket(const char *_host, uint16_t _port, struct sockaddr_in &sin_)
{
	struct hostent *hp = gethostbyname(_host);
	if(!hp) {
		m_error = Error::Hostname;
		throw std::runtime_error(str_format("host name lookup failed for '%s'", _host));
	}

	memset((char*)&sin_, 0, sizeof(sin_));
	#if HAVE_SOCKADDR_IN_SIN_LEN
	sin_.sin_len = sizeof(sin_);
	#endif
	memcpy((char*)&(sin_.sin_addr), hp->h_addr, hp->h_length);
	sin_.sin_family = hp->h_addrtype;
	sin_.sin_port = htons(_port);

	SOCKET socket_id = ::socket(AF_INET, SOCK_STREAM, 0);
	if(socket_id == INVALID_SOCKET) {
		m_error = Error::Socket;
		throw std::runtime_error(str_format("socket creation failed for '%s'", _host));
	}
	return socket_id;
}

std::pair<std::string,uint16_t> NetService::parse_address(std::string _address, uint16_t _default_port)
{
	std::string host;
	unsigned port;

	// find the last : in the address, interpret it as the port
	auto port_pos = _address.rfind(':');
	if(port_pos == _address.npos && !_default_port) {
		throw std::runtime_error("port number missing");
	} else if(port_pos != _address.npos) {
		try {
			port = std::stoul(_address.substr(port_pos + 1));
		} catch(...) {
			throw std::runtime_error("invalid port number");
		}
		host = _address.substr(0, port_pos);
	} else {
		host = _address;
		port = _default_port;
	}

	if(host.empty()) {
		throw std::runtime_error("invalid host name");
	}
	if(port > 65535) {
		throw std::runtime_error("port number must less than 65536");
	}
	
	return std::make_pair(host, uint16_t(port));
}

void NetService::open(const char *_host, uint16_t _port, Mode mode, uint64_t _conn_timeout_ms)
{
	if(!_host || !_port) {
		throw std::runtime_error("invalid host name or port number");
	}

	#ifdef _WIN32
	static bool winsock_init = false;
	if(!winsock_init) {
		WORD wVersionRequested;
		WSADATA wsaData;
		int err;
		wVersionRequested = MAKEWORD(2, 0);
		err = WSAStartup(wVersionRequested, &wsaData);
		if(err != 0) {
			throw std::runtime_error("WSAStartup failed");
		}
		PINFOF(LOG_V1, LOG_NET, "WinSock 2.0 initialized\n");
		winsock_init = true;
	}
	#endif

	if(m_client_socket != INVALID_SOCKET) {
		throw std::runtime_error("connection already established");
	}

	m_error = Error::NoError;

	switch(mode) {
		case Mode::Server: {
			if(m_server_socket != INVALID_SOCKET) {
				throw std::runtime_error("server already listening");
			}
			if(_port < 1024) {
				PWARNF(LOG_V0, LOG_NET, "%s: trying to open a server socket on a privileged port!\n", log_name());
			}
			m_server_host = _host;
			m_server_port = _port;
			struct sockaddr_in sin;
			SOCKET socket_id = create_socket(m_server_host.c_str(), m_server_port, sin);
			if(::bind(socket_id, (sockaddr*)&sin, sizeof(sin)) < 0 || ::listen(socket_id, SOMAXCONN) < 0) {
				closesocket(socket_id);
				m_error = Error::Listen;
				throw std::runtime_error(str_format("cannot listen to %s:%u", m_server_host.c_str(), m_server_port));
			}
			m_server_socket = socket_id;
			m_server_thread = std::thread(&NetService::start_net_server, this);
			PINFOF(LOG_V0, LOG_NET, "%s: net server initialized\n", log_name());
			break;
		}
		case Mode::Client: {
			m_client_host = _host;
			m_client_port = _port;
			struct sockaddr_in sin;
			SOCKET socket_id = create_socket(m_client_host.c_str(), m_client_port, sin);
			if(::connect(socket_id, (sockaddr*)&sin, sizeof(sin)) < 0) {
				closesocket(socket_id);
				m_error = Error::Connect;
				throw std::runtime_error(str_format("connection to %s failed", m_client_host.c_str()));
			}
			m_client_socket = socket_id;
			m_client_thread = std::thread(&NetService::start_net_client, this);
			PINFOF(LOG_V0, LOG_NET, "%s: net client initialized: connected to %s:%u\n",
					log_name(), m_client_host.c_str(), m_client_port);
			break;
		}
		case Mode::ClientAsync: {
			m_client_host = _host;
			m_client_port = _port;
			m_abort = false;
			PINFOF(LOG_V0, LOG_NET, "%s: net client: connecting to %s:%u ...\n",
					log_name(), m_client_host.c_str(), m_client_port);
			m_client_thread = std::thread(&NetService::start_net_client_async, this, _conn_timeout_ms);
			break;
		}
	}
}

void NetService::set_rx_queue(size_t _fifo_size, bool _oveflow)
{
	m_rx_data.set_max_size(_fifo_size);
	m_rx_overflow = _oveflow;
}

void NetService::set_tx_queue(size_t _fifo_size)
{
	m_tx_data.set_size(_fifo_size);
}

void NetService::set_tx_threshold(double _delay_ms, unsigned _bitrate)
{
	if(_delay_ms > SEND_MAX_DELAY_MS) {
		_delay_ms = SEND_MAX_DELAY_MS;
	} else if(_delay_ms < 0.0) {
		_delay_ms = 0.0;
	}
	unsigned threshold = 1;
	if(_delay_ms > .0) {
		threshold = TXFifo::ms_to_bytes(_delay_ms, _bitrate);
		if(threshold > DEFAULT_TX_FIFO_SIZE / 2) {
			threshold = DEFAULT_TX_FIFO_SIZE / 2;
			_delay_ms = TXFifo::bytes_to_ms(threshold, _bitrate);
		}
	}
	m_tx_data.set_threshold(threshold);
	m_tx_delay_ms = _delay_ms;
	PINFOF(LOG_V2, LOG_NET, "%s: tx buffer threshold:%u, delay:%.1fms\n",
			log_name(), m_tx_data.get_threshold(), m_tx_delay_ms);
}

void NetService::close()
{
	close_client();

	if(m_server_socket != INVALID_SOCKET) {
		// net server may be accepting connections
		::shutdown(m_server_socket, SD_BOTH);
		closesocket(m_server_socket);
	}
	if(m_server_thread.joinable()) {
		PDEBUGF(LOG_V1, LOG_NET, "%s: waiting for server thread...\n", log_name());
		m_server_thread.join();
	}
	m_server_socket = INVALID_SOCKET;
	m_server_accepted = false;
}

void NetService::close_client(bool _refuse)
{
	// abort any pending async client connections
	abort_connection();

	if(m_client_socket != INVALID_SOCKET) {
		// net server may be waiting on the client socket to be closed
		close_client_socket(Error::NoError, _refuse);
	}
	// this clear will empty the rx fifo, awakening the rx thread if stuck waiting for space
	clear_queues();
	if(m_client_thread.joinable()) {
		PDEBUGF(LOG_V1, LOG_NET, "%s: waiting for client thread...\n", log_name());
		m_client_thread.join();
	}
}

void NetService::close_client_socket(Error _error, bool _refuse)
{
	if(_error != Error::NoError) {
		m_error = _error;
	}
	if(m_client_socket == INVALID_SOCKET) {
		return;
	}
	PINFOF(LOG_V1, LOG_NET, "%s: %s the client connection\n", log_name(), _refuse ? "resetting":"closing");
	if(_refuse) {
		struct linger ling = {1, 0};
		::setsockopt(m_client_socket, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
	}
	::shutdown(m_client_socket, SD_BOTH);
	closesocket(m_client_socket);
	m_client_socket = INVALID_SOCKET;
}

void NetService::clear_queues()
{
	m_rx_data.clear();
	m_tx_data.clear();
}

void NetService::start_net_server()
{
	sockaddr addr;
	socklen_t addrlen = sizeof(sockaddr);

	PDEBUGF(LOG_V0, LOG_NET, "%s: server thread started\n", log_name());

	while(true) {

		m_server_accepted = false;

		PINFOF(LOG_V1, LOG_NET, "%s: waiting for client to connect to host:%s, port:%d\n",
				log_name(), m_server_host.c_str(), m_server_port);
		SOCKET client_sock = INVALID_SOCKET;
		if((client_sock = ::accept(m_server_socket, &addr, &addrlen)) == INVALID_SOCKET) {
			switch(get_neterr()) {
				#ifdef _WIN32
				case WSAECONNRESET:
				case WSAENETDOWN:
					PERRF(LOG_NET, "%s: connection failed\n", log_name());
					continue;
				#else
				case EPERM:        // firewall rules forbid connection
				case ECONNABORTED: // connection has been aborted.
					PERRF(LOG_NET, "%s: connection failed\n", log_name());
					continue;
				case ENETDOWN: case EPROTO: case ENOPROTOOPT: case EHOSTDOWN:
				case ENONET: case EHOSTUNREACH: case EOPNOTSUPP: case ENETUNREACH:
					// already-pending network errors, treat them like EAGAIN by retrying
					PWARNF(LOG_V0, LOG_NET, "%s: retrying connection ...\n", log_name());
					continue;
				#endif
				default:
					PINFOF(LOG_V1, LOG_NET, "%s: closing the net server (%d)\n", log_name(), get_neterr());
					return;
			}
		} else {
			char ip[INET6_ADDRSTRLEN] = "client";
			if(addr.sa_family == AF_INET) { 
				sockaddr_in *saddr_in = (sockaddr_in *)&addr;
				//char *ip = inet_ntoa(saddr_in->sin_addr);
				::inet_ntop(AF_INET, &(saddr_in->sin_addr), ip, INET_ADDRSTRLEN);
			} else if(addr.sa_family == AF_INET6) {
				sockaddr_in6 *saddr_in6 = (sockaddr_in6 *)&addr;
				::inet_ntop(AF_INET6, &(saddr_in6->sin6_addr), ip, INET6_ADDRSTRLEN);
			}

			if(!m_server_refuse && !is_connected()) {
				m_server_accepted = true;
				m_client_socket = client_sock;
				m_client_name = ip;

				auto mex = str_format("%s: %s connected", log_name(), m_client_name.c_str());
				PINFOF(LOG_V0, LOG_NET, "%s\n", mex.c_str());
				m_mex_callback(mex.c_str());

				net_data_loop();

				m_mex_callback(str_format("%s: %s disconnected", log_name(), m_client_name.c_str()));
			} else {
				PINFOF(LOG_V1, LOG_NET, "%s: refusing connection from %s\n", log_name(), ip);
				struct linger ling = {1, 0};
				::setsockopt(client_sock, SOL_SOCKET, SO_LINGER, (char *) &ling, sizeof(ling));
				closesocket(client_sock);
			}
		}
	}
	PDEBUGF(LOG_V0, LOG_NET, "%s: server thread terminated\n", log_name());
}

void NetService::start_net_client()
{
	PDEBUGF(LOG_V0, LOG_NET, "%s: client thread started\n", log_name());

	net_data_loop();

	m_mex_callback(str_format("%s: %s disconnected", log_name(), m_server_host.c_str()));
	PDEBUGF(LOG_V0, LOG_NET, "%s: client thread terminated\n", log_name());
}

void NetService::start_net_client_async(uint64_t _conn_timeout_ms)
{
	PDEBUGF(LOG_V0, LOG_NET, "%s: client thread started\n", log_name());

	struct sockaddr_in sin;
	SOCKET socket_id;
	try {
		socket_id = create_socket(m_client_host.c_str(), m_client_port, sin);
		PDEBUGF(LOG_V0, LOG_NET, "%s: net client: socket_id=%d.\n", log_name(), socket_id);
	} catch(std::runtime_error &e) {
		PERRF(LOG_NET, "%s: net client: %s.\n", log_name(), e.what());
		return;
	}

	auto fail_connection = [&](Error _error){
		closesocket(socket_id);
		m_error = _error;
	};

	#ifdef _WIN32

	u_long nonBlockingMode = 1;
	if(ioctlsocket(socket_id, FIONBIO, &nonBlockingMode) == SOCKET_ERROR) {
		PERRF(LOG_NET, "%s: net client: connection failed (ioctlsocket(FIONBIO) error %d).\n", log_name(), get_neterr());
		fail_connection(Error::Socket);
		return;
	}

	#else

	int prev_flags = ::fcntl(socket_id, F_GETFL, 0);
	if(prev_flags < 0) {
		PERRF(LOG_NET, "%s: net client: connection failed (fcntl(F_GETFL) error %d).\n", log_name(), get_neterr());
		fail_connection(Error::Socket);
		return;
	}
	if(::fcntl(socket_id, F_SETFL, prev_flags | O_NONBLOCK) < 0) {
		PERRF(LOG_NET, "%s: net client: connection failed (fcntl(O_NONBLOCK) error %d).\n", log_name(), get_neterr());
		fail_connection(Error::Socket);
		return;
	}

	#endif

	bool connected = false;
	bool timeout = false;
	if(::connect(socket_id, (sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
		int error = get_neterr();
		if((error != WOULD_BLOCK) && (error != IN_PROGRESS)) {
			fail_connection(Error::Connect);
			PERRF(LOG_NET, "%s: net client: connection failed (connect() error %d).\n", log_name(), error);
			return;
		}
		socklen_t len = sizeof(error);
		Chrono t;
		t.start();

		#ifdef _WIN32

		fd_set wrset;
		timeval tv;
		int ret = SOCKET_ERROR;
		do {
			if(t.elapsed_msec() >= _conn_timeout_ms) {
				timeout = true;
				break;
			}
			if(m_abort) {
				break;
			}
			// if using select() within a loop, the sets must be reinitialized before each call.
			FD_ZERO(&wrset);
			FD_SET(socket_id, &wrset);
			tv.tv_sec = 0;
			tv.tv_usec = 100000;
			ret = ::select(socket_id+1, NULL, &wrset, NULL, &tv);
		} while(ret == 0);

		if(::getsockopt(socket_id, SOL_SOCKET, SO_ERROR, (char*) &error, &len) != 0) {
			fail_connection(Error::Socket);
			PERRF(LOG_NET, "%s: net client: connection failed (unknown error).\n", log_name());
			return;
		}
		connected = (ret > 0) && FD_ISSET(socket_id, &wrset) && !error;

		#else

		struct pollfd fd[] = { { socket_id, POLLOUT, 0 } };
		do {
			// maybe use a self-pipe hack to send an abort signal? nah, this is much simpler
			while(::poll(fd, 1, 100) == 0) {
				if(t.elapsed_msec() >= _conn_timeout_ms) {
					timeout = true;
					break;
				}
				if(m_abort) {
					break;
				}
			}
			if(::getsockopt(socket_id, SOL_SOCKET, SO_ERROR, &error, &len) != 0) {
				fail_connection(Error::Socket);
				PERRF(LOG_NET, "%s: net client: connection failed (unknown error).\n", log_name());
				return;
			}
			connected = (fd[0].revents == POLLOUT);
			// poll() can return after 3s with EINPROGRESS 
		} while(!connected && !m_abort && !timeout && error == IN_PROGRESS);

		#endif

		PDEBUGF(LOG_V0, LOG_NET, "%s: connect time: %u\n", log_name(), t.elapsed_msec());
		if(!connected) {
			std::string cause;
			if(timeout) {
				fail_connection(Error::Aborted);
				cause = "time out";
			} else if(m_abort) {
				fail_connection(Error::Aborted);
				cause = "aborted";
			} else {
				switch(error) {
					#ifdef _WIN32
					case WSAECONNREFUSED: fail_connection(Error::HostRefused); break;
					case WSAETIMEDOUT: fail_connection(Error::Aborted); break;
					case WSAEHOSTDOWN: fail_connection(Error::HostDown); break;
					case WSAENETUNREACH:
					case WSAEHOSTUNREACH: fail_connection(Error::NoRoute); break;
					#else
					case ECONNREFUSED: fail_connection(Error::HostRefused); break;
					case ETIMEDOUT: fail_connection(Error::Aborted); break;
					case EHOSTDOWN: fail_connection(Error::HostDown); break;
					case ENETUNREACH:
					case EHOSTUNREACH: fail_connection(Error::NoRoute); break;
					#endif
					default:
						fail_connection(Error::Connect);
						break;
				}
				cause = str_format("failed: %s (%d)", get_neterr_str(error).c_str(), error);
			}
			PERRF(LOG_NET, "%s: net client: connection %s.\n", log_name(), cause.c_str());
			return;
		}
	} else {
		connected = true;
	}

	#ifdef _WIN32

	nonBlockingMode = 0;
	if(ioctlsocket(socket_id, FIONBIO, &nonBlockingMode) == SOCKET_ERROR) {
		fail_connection(Error::Socket);
		PERRF(LOG_NET, "%s: net client: connection failed (ioctlsocket(FIONBIO) error %d).\n", log_name(), get_neterr());
		return;
	}

	#else

	// restore old flags (make socket blocking again)
	if(::fcntl(socket_id, F_SETFL, prev_flags) < 0) {
		fail_connection(Error::Socket);
		PERRF(LOG_NET, "%s: net client: connection failed (fcntl(F_SETFL) error %d).\n", log_name(), get_neterr());
		return;
	}

	#endif

	m_client_socket = socket_id;

	m_mex_callback(str_format("%s: connected to %s:%u", log_name(), m_client_host.c_str(), m_client_port));
	PINFOF(LOG_V0, LOG_NET, "%s: net client initialized: connected to %s:%u\n",
			log_name(), m_client_host.c_str(), m_client_port);

	net_data_loop();

	m_mex_callback(str_format("%s: %s disconnected", log_name(), m_client_host.c_str()));
	PDEBUGF(LOG_V0, LOG_NET, "%s: client thread terminated\n", log_name());
}

void NetService::net_data_loop()
{
	if(m_tcp_nodelay) {
		PDEBUGF(LOG_V1, LOG_NET, "%s: setting TCP_NODELAY ...\n", log_name());
		int enabled = 1;
		if(::setsockopt(m_client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&enabled, sizeof(enabled)) != 0) {
			PERRF(LOG_NET, "%s: error setting TCP_NODELAY option (%d)\n", log_name(), get_neterr());
			close_client_socket(Error::Socket);
			return;
		}
	}

	PDEBUGF(LOG_V1, LOG_NET, "%s: starting tx thread ...\n", log_name());
	auto tx_thread = std::thread(&NetService::net_tx_loop, this);

	while(m_client_socket != INVALID_SOCKET) {
		uint8_t data[MIN_RX_FIFO_SIZE];
		ssize_t bytes = (ssize_t)::recv(m_client_socket, (char*)&data, MIN_RX_FIFO_SIZE, 0);
		if(bytes > 0) {
			if(!m_rx_overflow) {
				m_rx_data.wait_for_space(bytes);
			}
			bool result = m_rx_data.force_push(data, bytes);
			PDEBUGF(LOG_V2, LOG_NET, "%s: sock read (%u): [ ", log_name(), bytes);
			for(ssize_t i=0; i<bytes; i++) {
				PDEBUGF(LOG_V2, LOG_NET, "%02x ", data[i]);
			}
			PDEBUGF(LOG_V2, LOG_NET, "] %s\n", !result?"overflow":"");
		} else {
			PINFOF(LOG_V0, LOG_NET, "%s: connection terminated", log_name());
			if(bytes < 0) {
				PINFOF(LOG_V0, LOG_NET, " (%u)", get_neterr());
			}
			PINFOF(LOG_V0, LOG_NET, "\n");
			break;
		}
	}
	close_client_socket(Error::Terminated);

	tx_thread.join();
	PDEBUGF(LOG_V1, LOG_NET, "%s: tx thread terminated\n", log_name());
}

void NetService::net_tx_loop()
{
	std::vector<uint8_t> tx_buf(DEFAULT_TX_FIFO_SIZE);

	while(m_client_socket != INVALID_SOCKET) {
		uint64_t wait_ns = SEND_MAX_DELAY_NS;
		if(m_tx_delay_ms > 0.0) {
			wait_ns = m_tx_delay_ms * 1_ms;
			if(m_cycles_factor < 1.0) {
				// if the machine is slowed down we need to wait more for the same amount of data
				wait_ns *= (1.0 / m_cycles_factor);
			}
			if(wait_ns > SEND_MAX_DELAY_NS) {
				wait_ns = SEND_MAX_DELAY_NS;
			}
		}
		size_t len = m_tx_data.read(&tx_buf[0], DEFAULT_TX_FIFO_SIZE, wait_ns);
		if(len) {
			PDEBUGF(LOG_V2, LOG_NET, "%s: sock write (%u): [ ", log_name(), len);
			for(size_t i=0; i<len; i++) {
				PDEBUGF(LOG_V2, LOG_NET, "%02x ", tx_buf[i]);
			}
			PDEBUGF(LOG_V2, LOG_NET, "]\n");
			ssize_t res = (ssize_t)::send(m_client_socket, (const char*)&tx_buf[0], len, 0);
			if(res < 0) {
				PDEBUGF(LOG_V0, LOG_NET, "%s: send() error: %u\n", log_name(), get_neterr());
			} else if(size_t(res) != len) {
				PDEBUGF(LOG_V0, LOG_NET, "%s: tx bytes: %u, sent bytes: %u, errno: %u\n", log_name(), len, res, get_neterr());
			}
		}
	}
}



