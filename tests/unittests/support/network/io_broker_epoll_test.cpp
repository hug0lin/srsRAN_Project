/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsran/adt/optional.h"
#include "srsran/support/io/io_broker_factory.h"
#include <condition_variable>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/un.h> // for unix sockets

using namespace srsran;

static const std::string tx_buf = "hello world!";

class io_broker_epoll : public ::testing::Test
{
protected:
  void SetUp() override { epoll_broker = create_io_broker(io_broker_type::epoll); }

  void TearDown() override
  {
    EXPECT_TRUE(epoll_broker->unregister_fd(socket_fd));
    if (socket_fd > 0) {
      close(socket_fd);
    }
    socket_fd = 0;
  }

  void data_receive_callback(int fd)
  {
    std::lock_guard<std::mutex> lock(rx_mutex);
    // receive data on provided fd
    char rx_buf[1024];
    int  bytes = read(fd, rx_buf, sizeof(rx_buf));

    total_rx_bytes += bytes;

    if (socket_type == SOCK_DGRAM) {
      ASSERT_EQ(bytes, tx_buf.length());
    }
    rx_cvar.notify_one();
  }

  void create_unix_sockets()
  {
    // check temporary files for IPC
    std::string socket_filename = "/tmp/io_broker_server";
    ASSERT_LE(socket_filename.size(), sizeof(sockaddr_un::sun_path));

    // delete exsiting file (if any)
    int ret = remove(socket_filename.c_str());
    if (ret == -1 && errno != ENOENT) {
      // allow ENOENT, i.e. file not found
      ASSERT_NE(ret, -1);
    }

    // create server socket
    socket_fd   = socket(AF_UNIX, SOCK_DGRAM, 0);
    socket_type = SOCK_DGRAM;
    ASSERT_NE(socket_fd, -1);

    // prepare server address
    // memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr_un.sun_family = AF_UNIX;
    strncpy(server_addr_un.sun_path, socket_filename.c_str(), socket_filename.length());

    // bind server
    ret = bind(socket_fd, (struct sockaddr*)&server_addr_un, sizeof(server_addr_un));
    ASSERT_NE(ret, -1);

    // listen+accept?
    // ret = listen(socket_fd,1);
    // ASSERT_NE(ret, -1);

    // prepare client address
    client_addr_un.sun_family = AF_UNIX;
    strncpy(client_addr_un.sun_path, socket_filename.c_str(), socket_filename.length());

    // connect client to server_filename
    ret = connect(socket_fd, (struct sockaddr*)&server_addr_un, sizeof(server_addr_un));
    // perror("socket failed");
    ASSERT_NE(ret, -1);
  }

  optional<uint16_t> get_bind_port(int sock_fd)
  {
    if (sock_fd == -1) {
      return {};
    }

    sockaddr_storage gw_addr_storage;
    sockaddr*        gw_addr     = (sockaddr*)&gw_addr_storage;
    socklen_t        gw_addr_len = sizeof(gw_addr_storage);

    int ret = getsockname(sock_fd, gw_addr, &gw_addr_len);
    if (ret != 0) {
      return {};
    }

    uint16_t bind_port;
    if (gw_addr->sa_family == AF_INET) {
      bind_port = ntohs(((sockaddr_in*)gw_addr)->sin_port);
    } else if (gw_addr->sa_family == AF_INET6) {
      bind_port = ntohs(((sockaddr_in6*)gw_addr)->sin6_port);
    } else {
      return {};
    }

    return bind_port;
  }

  void create_af_init_sockets(int type)
  {
    // create server socket
    socket_fd   = socket(AF_INET, type, 0);
    socket_type = type;
    ASSERT_NE(socket_fd, -1);

    // configure socket as reusable to allow multiple runs
    int enable = 1;
    ASSERT_NE(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)), -1);

    // prepare server address
    memset(&server_addr_in, 0, sizeof(struct sockaddr_in));
    server_addr_in.sin_family      = AF_INET;
    server_addr_in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr_in.sin_port        = htons(0);

    // bind server
    int ret = bind(socket_fd, (struct sockaddr*)&server_addr_in, sizeof(server_addr_in));
    // perror("socket failed");
    ASSERT_NE(ret, -1);

    // get bind port
    optional<uint16_t> port = get_bind_port(socket_fd);
    ASSERT_TRUE(port.has_value());
    ASSERT_NE(port.value(), 0);
    // update server address
    server_addr_in.sin_port = htons(port.value());

    // prepare client address
    memset(&client_addr_in, 0, sizeof(struct sockaddr_in));
    client_addr_in.sin_family      = AF_INET;
    client_addr_in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    client_addr_in.sin_port        = htons(port.value());

    // connect client to server
    ret = connect(socket_fd, (struct sockaddr*)&server_addr_in, sizeof(server_addr_in));
    ASSERT_NE(ret, -1);
  }

  void add_socket_to_epoll()
  {
    ASSERT_TRUE(epoll_broker->register_fd(socket_fd, [this](int fd) { data_receive_callback(fd); }));
  }

  void send_on_socket() const
  {
    // send text
    int ret = send(socket_fd, tx_buf.c_str(), tx_buf.length(), 0);
    ASSERT_EQ(ret, tx_buf.length());
  }

  void run_tx_rx_test(std::chrono::milliseconds timeout_ms = std::chrono::milliseconds(1000))
  {
    const int count = 5;
    int       run   = count;
    while (run-- > 0) {
      send_on_socket();
    }

    // wait until all bytes are received
    std::unique_lock<std::mutex> lock(rx_mutex);
    if (!rx_cvar.wait_for(lock, timeout_ms, [this]() { return total_rx_bytes >= tx_buf.length() * count; })) {
      FAIL() << "Timeout: received only " << total_rx_bytes << " of " << tx_buf.length() * count << " Bytes.";
    }
    ASSERT_EQ(total_rx_bytes, tx_buf.length() * count);
  }

private:
  std::unique_ptr<io_broker> epoll_broker;
  int                        socket_fd   = 0;
  int                        socket_type = 0;

  // unix domain socket addresses (used by unix sockets only)
  struct sockaddr_un server_addr_un = {};
  struct sockaddr_un client_addr_un = {};

  // inet socket addresses (used by all other protocols)
  struct sockaddr_in server_addr_in = {};
  struct sockaddr_in client_addr_in = {};

  std::mutex              rx_mutex;
  std::condition_variable rx_cvar;

  size_t total_rx_bytes = 0;
};

TEST_F(io_broker_epoll, unix_socket_trx_test)
{
  create_unix_sockets();
  add_socket_to_epoll();
  run_tx_rx_test();
}

TEST_F(io_broker_epoll, af_inet_socket_udp_trx_test)
{
  create_af_init_sockets(SOCK_DGRAM);
  add_socket_to_epoll();
  run_tx_rx_test();
}

TEST_F(io_broker_epoll, af_inet_socket_tcp_trx_test)
{
  create_af_init_sockets(SOCK_STREAM);
  add_socket_to_epoll();
  run_tx_rx_test();
}
