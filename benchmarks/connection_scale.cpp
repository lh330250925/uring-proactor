#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

enum class State
{
    Connecting,
    Idle,
    Sending,
    Receiving,
    Complete,
    Failed
};

struct Connection
{
    int fd = -1;
    State state = State::Failed;
    unsigned int sent = 0;
    unsigned int received = 0;
};

struct Config
{
    const char *address = "127.0.0.1";
    int port = 8080;
    int connections = 10000;
    int hold_seconds = 2;
    int payload_size = 64;
    int batch_size = 256;
    int timeout_seconds = 30;
};

bool parse_positive(const char *text, int &value)
{
    char *end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed <= 0 || parsed > 1000000)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

void print_help()
{
    std::puts(
        "Usage: connection_scale [options]\n"
        "  --addr ADDR          server address (default: 127.0.0.1)\n"
        "  --port PORT          TCP port (default: 8080)\n"
        "  --connections N      simultaneous connections (default: 10000)\n"
        "  --hold S             idle hold time before echo (default: 2)\n"
        "  --payload-size N     bytes echoed per connection (default: 64)\n"
        "  --batch-size N       concurrently activated connections (default: 256)\n"
        "  --timeout S          connect/echo deadline (default: 30)");
}

bool update_events(int epoll_fd, unsigned int index, const Connection &connection,
                   uint32_t events)
{
    epoll_event event{};
    event.events = events | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    event.data.u32 = index;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, connection.fd, &event) == 0;
}

void fail_connection(Connection &connection)
{
    if (connection.fd >= 0)
        close(connection.fd);
    connection.fd = -1;
    connection.state = State::Failed;
}

double seconds_between(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double>(end - start).count();
}
}

int main(int argc, char **argv)
{
    Config config;
    for (int i = 1; i < argc; ++i)
    {
        std::string option = argv[i];
        if (option == "--addr" && i + 1 < argc)
            config.address = argv[++i];
        else if (option == "--port" && i + 1 < argc && parse_positive(argv[i + 1], config.port))
            ++i;
        else if (option == "--connections" && i + 1 < argc && parse_positive(argv[i + 1], config.connections))
            ++i;
        else if (option == "--hold" && i + 1 < argc && parse_positive(argv[i + 1], config.hold_seconds))
            ++i;
        else if (option == "--payload-size" && i + 1 < argc && parse_positive(argv[i + 1], config.payload_size))
            ++i;
        else if (option == "--batch-size" && i + 1 < argc && parse_positive(argv[i + 1], config.batch_size))
            ++i;
        else if (option == "--timeout" && i + 1 < argc && parse_positive(argv[i + 1], config.timeout_seconds))
            ++i;
        else if (option == "-h" || option == "--help")
        {
            print_help();
            return 0;
        }
        else
        {
            std::fprintf(stderr, "Invalid option or value: %s\n", option.c_str());
            return 2;
        }
    }

    rlimit fd_limit{};
    if (getrlimit(RLIMIT_NOFILE, &fd_limit) != 0 ||
        fd_limit.rlim_cur < static_cast<rlim_t>(config.connections + 32))
    {
        std::fprintf(stderr, "Open-file limit is too low for %d connections.\n",
                     config.connections);
        return 2;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<uint16_t>(config.port));
    if (inet_pton(AF_INET, config.address, &server.sin_addr) != 1)
    {
        std::fprintf(stderr, "Invalid IPv4 address: %s\n", config.address);
        return 2;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0)
    {
        std::perror("epoll_create1");
        return 1;
    }

    std::vector<Connection> connections(config.connections);
    std::vector<epoll_event> events(std::min(config.connections, 4096));
    int connected = 0;
    int failed = 0;
    auto connect_start = Clock::now();

    for (int index = 0; index < config.connections; ++index)
    {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
        {
            ++failed;
            continue;
        }

        connections[index].fd = fd;
        int result = connect(fd, reinterpret_cast<sockaddr *>(&server), sizeof(server));
        if (result == 0)
        {
            connections[index].state = State::Idle;
            ++connected;
        }
        else if (errno == EINPROGRESS)
            connections[index].state = State::Connecting;
        else
        {
            fail_connection(connections[index]);
            ++failed;
            continue;
        }

        epoll_event event{};
        event.events = (connections[index].state == State::Connecting ? EPOLLOUT : EPOLLIN) |
                       EPOLLERR | EPOLLHUP | EPOLLRDHUP;
        event.data.u32 = static_cast<unsigned int>(index);
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0)
        {
            bool was_connected = connections[index].state == State::Idle;
            fail_connection(connections[index]);
            if (was_connected)
                --connected;
            ++failed;
        }
    }

    auto connect_deadline = connect_start + std::chrono::seconds(config.timeout_seconds);
    while (connected + failed < config.connections && Clock::now() < connect_deadline)
    {
        int ready = epoll_wait(epoll_fd, events.data(), events.size(), 100);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0)
            break;
        for (int i = 0; i < ready; ++i)
        {
            Connection &connection = connections[events[i].data.u32];
            if (connection.state != State::Connecting)
                continue;
            int socket_error = 0;
            socklen_t error_size = sizeof(socket_error);
            if (getsockopt(connection.fd, SOL_SOCKET, SO_ERROR,
                           &socket_error, &error_size) == 0 && socket_error == 0)
            {
                connection.state = State::Idle;
                ++connected;
                if (!update_events(epoll_fd, events[i].data.u32, connection, EPOLLIN))
                {
                    fail_connection(connection);
                    --connected;
                    ++failed;
                }
            }
            else
            {
                fail_connection(connection);
                ++failed;
            }
        }
    }

    for (auto &connection : connections)
    {
        if (connection.state == State::Connecting)
        {
            fail_connection(connection);
            ++failed;
        }
    }
    auto connect_end = Clock::now();
    double connect_seconds = seconds_between(connect_start, connect_end);

    std::printf("TCP connection scale benchmark\n");
    std::printf("  target:           %d\n", config.connections);
    std::printf("  established:      %d\n", connected);
    std::printf("  failed:           %d\n", failed);
    std::printf("  connect time:     %.3f s\n", connect_seconds);
    std::printf("  connect rate:     %.0f conn/s\n",
                connect_seconds > 0.0 ? connected / connect_seconds : 0.0);

    if (connected != config.connections)
    {
        close(epoll_fd);
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(config.hold_seconds));

    std::vector<char> send_buffer(config.payload_size);
    std::vector<char> receive_buffer(static_cast<std::size_t>(config.connections) * config.payload_size);
    int echoed = 0;
    int echo_failed = 0;
    auto echo_start = Clock::now();
    for (int batch_start = 0; batch_start < config.connections;
         batch_start += config.batch_size)
    {
        int batch_end = std::min(batch_start + config.batch_size, config.connections);
        int batch_count = batch_end - batch_start;
        int batch_target = echoed + echo_failed + batch_count;
        for (int index = batch_start; index < batch_end; ++index)
        {
            connections[index].state = State::Sending;
            if (!update_events(epoll_fd, index, connections[index], EPOLLOUT))
            {
                fail_connection(connections[index]);
                ++echo_failed;
            }
        }

        auto batch_deadline = Clock::now() + std::chrono::seconds(config.timeout_seconds);
        while (echoed + echo_failed < batch_target && Clock::now() < batch_deadline)
        {
            int ready = epoll_wait(epoll_fd, events.data(), events.size(), 100);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0)
                break;
            for (int i = 0; i < ready; ++i)
            {
                unsigned int index = events[i].data.u32;
                Connection &connection = connections[index];
                if (connection.state == State::Sending)
                {
                    for (int offset = 0; offset < config.payload_size; ++offset)
                        send_buffer[offset] = static_cast<char>((index + offset) % 251);
                    ssize_t result = send(connection.fd,
                                          send_buffer.data() + connection.sent,
                                          send_buffer.size() - connection.sent,
                                          MSG_NOSIGNAL);
                    if (result > 0)
                    {
                        connection.sent += static_cast<unsigned int>(result);
                        if (connection.sent == send_buffer.size())
                        {
                            connection.state = State::Receiving;
                            update_events(epoll_fd, index, connection, EPOLLIN);
                        }
                    }
                    else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        fail_connection(connection);
                        ++echo_failed;
                    }
                }
                else if (connection.state == State::Receiving)
                {
                    char *target = receive_buffer.data() +
                                   static_cast<std::size_t>(index) * config.payload_size;
                    ssize_t result = recv(connection.fd, target + connection.received,
                                          config.payload_size - connection.received, 0);
                    if (result > 0)
                    {
                        connection.received += static_cast<unsigned int>(result);
                        if (connection.received == static_cast<unsigned int>(config.payload_size))
                        {
                            bool valid = true;
                            for (int offset = 0; offset < config.payload_size; ++offset)
                                valid &= target[offset] == static_cast<char>((index + offset) % 251);
                            connection.state = valid ? State::Complete : State::Failed;
                            if (valid)
                                ++echoed;
                            else
                                ++echo_failed;
                        }
                    }
                    else if (result == 0 || (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                    {
                        fail_connection(connection);
                        ++echo_failed;
                    }
                }
            }
        }

        for (int index = batch_start; index < batch_end; ++index)
        {
            Connection &connection = connections[index];
            if (connection.state == State::Sending || connection.state == State::Receiving)
            {
                fail_connection(connection);
                ++echo_failed;
            }
        }
    }

    for (auto &connection : connections)
    {
        if (connection.fd >= 0)
            close(connection.fd);
    }
    close(epoll_fd);

    double echo_seconds = seconds_between(echo_start, Clock::now());
    std::printf("  hold time:        %d s\n", config.hold_seconds);
    std::printf("  echo payload:     %d B/connection\n", config.payload_size);
    std::printf("  activation batch: %d connections\n", config.batch_size);
    std::printf("  echo completed:   %d\n", echoed);
    std::printf("  echo failed:      %d\n", echo_failed);
    std::printf("  echo phase time:  %.3f s\n", echo_seconds);
    return echoed == connected && echo_failed == 0 ? 0 : 1;
}