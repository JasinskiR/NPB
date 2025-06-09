#include <iostream>
#include <thread>
#include <future>
#include <chrono>
#include <atomic>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <memory>
#include <random>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sstream>
#include <getopt.h>

// Async headers
#if __cplusplus >= 202002L
#include <coroutine>
#define HAS_COROUTINES 1
#else
#define HAS_COROUTINES 0
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/event.h>
#include <sys/time.h>
#else
#include <sys/epoll.h>
#endif

using namespace std::chrono;

// Command line arguments structure
struct BenchmarkArgs {
    size_t num_clients = 50;
    size_t messages_per_client = 100;
    size_t max_connections = 1000;
    size_t message_size_kb = 0;
    size_t num_threads = 0;  // 0 means use hardware_concurrency()
    bool use_async = false;  // New flag for async mode
};

// Function to parse command line arguments
BenchmarkArgs parse_args(int argc, char* argv[]) {
    BenchmarkArgs args;
    
    static struct option long_options[] = {
        {"num-clients", required_argument, 0, 'c'},
        {"messages-per-client", required_argument, 0, 'm'},
        {"max-connections", required_argument, 0, 'x'},
        {"message-size-kb", required_argument, 0, 's'},
        {"num-threads", required_argument, 0, 't'},
        {"async", no_argument, 0, 'a'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int c;
    while ((c = getopt_long(argc, argv, "c:m:x:s:t:ah", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                args.num_clients = std::stoul(optarg);
                break;
            case 'm':
                args.messages_per_client = std::stoul(optarg);
                break;
            case 'x':
                args.max_connections = std::stoul(optarg);
                break;
            case 's':
                args.message_size_kb = std::stoul(optarg);
                break;
            case 't':
                args.num_threads = std::stoul(optarg);
                break;
            case 'a':
                args.use_async = true;
                break;
            case 'h':
                std::cout << "Usage: " << argv[0] << " [OPTIONS]\n\n"
                          << "Options:\n"
                          << "  --num-clients, -c NUM       Number of echo clients to spawn (default: 50)\n"
                          << "  --messages-per-client, -m NUM  Number of messages per client (default: 100)\n"
                          << "  --max-connections, -x NUM   Maximum number of concurrent connections (default: 1000)\n"
                          << "  --message-size-kb, -s SIZE  Size of message payload in KB (default: 0 for small messages)\n"
                          << "  --num-threads, -t NUM       Number of worker threads (default: hardware concurrency)\n"
                          << "  --async, -a                 Use async I/O instead of threads\n"
                          << "  --help, -h                  Show this help message\n";
                exit(0);
            default:
                std::cerr << "Try '" << argv[0] << " --help' for more information.\n";
                exit(1);
        }
    }
    
    return args;
}

class AsyncMetrics {
private:
    std::atomic<size_t> task_spawns{0};
    std::atomic<uint64_t> task_spawn_times{0};
    std::atomic<size_t> async_operations{0};
    std::atomic<uint64_t> async_operation_times{0};
    std::vector<uint64_t> memory_snapshots;
    std::mutex memory_mutex;
    steady_clock::time_point start_time;

public:
    AsyncMetrics() : start_time(steady_clock::now()) {}
    
    void record_task_spawn(const duration<double, std::nano>& duration) {
        task_spawns.fetch_add(1, std::memory_order_relaxed);
        task_spawn_times.fetch_add(static_cast<uint64_t>(duration.count()), std::memory_order_relaxed);
    }
    
    void record_async_operation(const duration<double, std::nano>& duration) {
        async_operations.fetch_add(1, std::memory_order_relaxed);
        async_operation_times.fetch_add(static_cast<uint64_t>(duration.count()), std::memory_order_relaxed);
    }
    
    void take_memory_snapshot() {
        if (auto memory_kb = get_current_memory_usage()) {
            std::lock_guard<std::mutex> lock(memory_mutex);
            memory_snapshots.push_back(*memory_kb);
        }
    }
    
    void print_metrics(const std::string& test_name) {
        auto elapsed = steady_clock::now() - start_time;
        auto task_spawns_val = task_spawns.load(std::memory_order_relaxed);
        auto task_spawn_times_val = task_spawn_times.load(std::memory_order_relaxed);
        auto async_ops = async_operations.load(std::memory_order_relaxed);
        auto async_op_times = async_operation_times.load(std::memory_order_relaxed);
        
        double elapsed_seconds = duration_cast<microseconds>(elapsed).count() / 1000000.0;
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "ASYNC METRICS: " << test_name << "\n";
        std::cout << std::string(60, '=') << "\n";
        
        std::cout << "EXECUTION TIME: " << elapsed_seconds << " seconds\n";
        
        if (task_spawns_val > 0) {
            std::cout << "\nTASK SPAWNING:\n";
            std::cout << "  Total tasks spawned: " << task_spawns_val << "\n";
            std::cout << "  Avg spawn time: " 
                      << (task_spawn_times_val / static_cast<double>(task_spawns_val) / 1000.0) 
                      << " μs\n";
            std::cout << "  Tasks per second: " 
                      << (task_spawns_val / elapsed_seconds) << "\n";
        }
        
        if (async_ops > 0) {
            std::cout << "\nASYNC OPERATIONS:\n";
            std::cout << "  Total operations: " << async_ops << "\n";
            std::cout << "  Avg operation time: " 
                      << (async_op_times / static_cast<double>(async_ops) / 1000.0) 
                      << " μs\n";
            std::cout << "  Operations per second: " 
                      << (async_ops / elapsed_seconds) << "\n";
        }
        
        {
            std::lock_guard<std::mutex> lock(memory_mutex);
            if (!memory_snapshots.empty()) {
                auto min_mem = *std::min_element(memory_snapshots.begin(), memory_snapshots.end()) / 1024.0;
                auto max_mem = *std::max_element(memory_snapshots.begin(), memory_snapshots.end()) / 1024.0;
                auto avg_mem = std::accumulate(memory_snapshots.begin(), memory_snapshots.end(), 0ULL) 
                              / static_cast<double>(memory_snapshots.size()) / 1024.0;
                
                std::cout << "\nMEMORY USAGE:\n";
                std::cout << "  Min: " << min_mem << " MB\n";
                std::cout << "  Max: " << max_mem << " MB\n";
                std::cout << "  Avg: " << avg_mem << " MB\n";
                std::cout << "  Growth: " << (max_mem - min_mem) << " MB\n";
            }
        }
    }

private:
    std::optional<uint64_t> get_current_memory_usage() {
#ifdef __APPLE__
        task_vm_info_data_t vmInfo;
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmInfo, &count) == KERN_SUCCESS) {
            return vmInfo.phys_footprint / 1024;
        }
#else
        std::ifstream status_file("/proc/self/status");
        std::string line;
        while (std::getline(status_file, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line);
                std::string key, value, unit;
                iss >> key >> value >> unit;
                return std::stoull(value);
            }
        }
#endif
        return std::nullopt;
    }
};

class EchoServerMetrics {
private:
    std::atomic<size_t> connections_accepted{0};
    std::atomic<size_t> messages_echoed{0};
    std::atomic<uint64_t> bytes_transferred{0};
    std::atomic<uint64_t> connection_durations{0};
    std::atomic<size_t> active_connections{0};
    std::atomic<size_t> peak_connections{0};
    steady_clock::time_point start_time;

public:
    EchoServerMetrics() : start_time(steady_clock::now()) {}
    
    size_t connection_started() {
        auto conn_id = connections_accepted.fetch_add(1, std::memory_order_relaxed);
        auto active = active_connections.fetch_add(1, std::memory_order_relaxed) + 1;
        
        auto peak = peak_connections.load(std::memory_order_relaxed);
        while (active > peak) {
            if (peak_connections.compare_exchange_weak(peak, active, std::memory_order_relaxed)) {
                break;
            }
        }
        
        return conn_id;
    }
    
    void connection_ended(const duration<double, std::micro>& duration, size_t messages, uint64_t bytes) {
        active_connections.fetch_sub(1, std::memory_order_relaxed);
        messages_echoed.fetch_add(messages, std::memory_order_relaxed);
        bytes_transferred.fetch_add(bytes, std::memory_order_relaxed);
        connection_durations.fetch_add(static_cast<uint64_t>(duration.count()), std::memory_order_relaxed);
    }
    
    void print_metrics(const std::string& mode) {
        auto elapsed = steady_clock::now() - start_time;
        auto connections = connections_accepted.load(std::memory_order_relaxed);
        auto messages = messages_echoed.load(std::memory_order_relaxed);
        auto bytes = bytes_transferred.load(std::memory_order_relaxed);
        auto total_duration = connection_durations.load(std::memory_order_relaxed);
        auto peak_conn = peak_connections.load(std::memory_order_relaxed);
        auto active_conn = active_connections.load(std::memory_order_relaxed);
        
        double elapsed_seconds = duration_cast<microseconds>(elapsed).count() / 1000000.0;
        
        std::cout << "\n" << mode << " ECHO SERVER METRICS:\n";
        std::cout << std::string(60, '=') << "\n";
        std::cout << "DURATION: " << elapsed_seconds << " seconds\n";
        
        std::cout << "\nCONNECTIONS:\n";
        std::cout << "  Total: " << connections << "\n";
        std::cout << "  Active: " << active_conn << "\n";
        std::cout << "  Peak concurrent: " << peak_conn << "\n";
        std::cout << "  Rate: " << (connections / elapsed_seconds) << " conn/s\n";
        if (connections > 0) {
            std::cout << "  Avg duration: " << (total_duration / static_cast<double>(connections) / 1000.0) << " ms\n";
        }
        
        std::cout << "\nTHROUGHPUT:\n";
        std::cout << "  Messages: " << messages << "\n";
        std::cout << "  Messages/s: " << (messages / elapsed_seconds) << "\n";
        std::cout << "  Bytes: " << bytes << " (" << (bytes / (1024.0 * 1024.0)) << " MB)\n";
        std::cout << "  Throughput: " << (bytes / (1024.0 * 1024.0) / elapsed_seconds) << " MB/s\n";
        
        if (messages > 0 && connections > 0) {
            std::cout << "\nEFFICIENCY:\n";
            std::cout << "  Avg bytes/message: " << (bytes / static_cast<double>(messages)) << "\n";
            std::cout << "  Messages/connection: " << (messages / static_cast<double>(connections)) << "\n";
        }
    }
};

// Async echo server using event loops
class AsyncEchoServer {
private:
    int server_socket;
    std::atomic<bool> running{true};
    std::shared_ptr<EchoServerMetrics> metrics;
    size_t max_connections;
    std::atomic<size_t> current_connections{0};
    
    struct ClientConnection {
        int socket;
        std::string address;
        steady_clock::time_point start_time;
        uint64_t total_bytes;
        size_t message_count;
        size_t conn_id;
        
        ClientConnection(int sock, const std::string& addr, size_t id) 
            : socket(sock), address(addr), start_time(steady_clock::now()), 
              total_bytes(0), message_count(0), conn_id(id) {}
    };
    
    std::vector<std::unique_ptr<ClientConnection>> active_clients;
    std::mutex clients_mutex;

#ifdef __APPLE__
    int kqueue_fd;
#else
    int epoll_fd;
#endif

public:
    AsyncEchoServer(const std::string& address, int port, size_t max_conn) 
        : max_connections(max_conn), metrics(std::make_shared<EchoServerMetrics>()) {
        
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        // Set non-blocking
        int flags = fcntl(server_socket, F_GETFL, 0);
        fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);
        
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr);
        
        if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            throw std::runtime_error("Failed to bind socket");
        }
        
        if (listen(server_socket, SOMAXCONN) < 0) {
            throw std::runtime_error("Failed to listen on socket");
        }
        
#ifdef __APPLE__
        kqueue_fd = kqueue();
        if (kqueue_fd == -1) {
            throw std::runtime_error("Failed to create kqueue");
        }
        
        struct kevent ev;
        EV_SET(&ev, server_socket, EVFILT_READ, EV_ADD, 0, 0, nullptr);
        kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
#else
        epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            throw std::runtime_error("Failed to create epoll");
        }
        
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = server_socket;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev);
#endif
    }
    
    ~AsyncEchoServer() {
        close(server_socket);
#ifdef __APPLE__
        close(kqueue_fd);
#else
        close(epoll_fd);
#endif
    }
    
    bool handle_client_data(ClientConnection* client) {
        char buffer[1024];
        ssize_t bytes_read = recv(client->socket, buffer, sizeof(buffer), 0);
        
        if (bytes_read > 0) {
            ssize_t bytes_sent = send(client->socket, buffer, bytes_read, 0);
            if (bytes_sent > 0) {
                client->total_bytes += bytes_read + bytes_sent;
                client->message_count++;
                return true; // Continue processing
            } else {
                return false; // Send failed, disconnect
            }
        } else if (bytes_read == 0 || (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Connection closed or error
            return false;
        }
        return true; // EAGAIN/EWOULDBLOCK, continue processing
    }
    
    void disconnect_client(int client_socket, ClientConnection* client_info) {
        auto duration = duration_cast<microseconds>(steady_clock::now() - client_info->start_time);
        metrics->connection_ended(duration, client_info->message_count, client_info->total_bytes);
        current_connections.fetch_sub(1, std::memory_order_relaxed);
        
        std::cout << "Async client disconnected: " << client_info->address 
                  << " (ID: " << client_info->conn_id 
                  << ", " << duration_cast<milliseconds>(duration).count() << "ms"
                  << ", " << client_info->message_count << " messages"
                  << ", " << client_info->total_bytes << " bytes)\n";
        
        close(client_socket);
    }
    
    void run_async() {
        std::cout << "Async Echo server listening (max " << max_connections << " connections)\n";
        
        while (running.load(std::memory_order_relaxed)) {
            std::vector<int> clients_to_remove;
            
#ifdef __APPLE__
            struct kevent events[64];
            struct timespec timeout = {0, 1000000}; // 1ms
            int nev = kevent(kqueue_fd, nullptr, 0, events, 64, &timeout);
            
            for (int i = 0; i < nev; ++i) {
                if ((int)events[i].ident == server_socket) {
                    // New connection
                    sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);
                    
                    int client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);
                    if (client_socket >= 0) {
                        if (current_connections.load(std::memory_order_relaxed) >= max_connections) {
                            std::cout << "Async connection rejected: max capacity reached\n";
                            close(client_socket);
                            continue;
                        }
                        
                        // Set non-blocking
                        int flags = fcntl(client_socket, F_GETFL, 0);
                        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
                        
                        char client_ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                        std::string client_address = std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
                        
                        auto conn_id = metrics->connection_started();
                        current_connections.fetch_add(1, std::memory_order_relaxed);
                        
                        std::cout << "Async client connected: " << client_address << " (ID: " << conn_id << ")\n";
                        
                        {
                            std::lock_guard<std::mutex> lock(clients_mutex);
                            auto client = std::make_unique<ClientConnection>(client_socket, client_address, conn_id);
                            active_clients.push_back(std::move(client));
                        }
                        
                        // Add to kqueue
                        struct kevent ev;
                        EV_SET(&ev, client_socket, EVFILT_READ, EV_ADD, 0, 0, nullptr);
                        kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
                    }
                } else {
                    // Client data
                    int client_socket = (int)events[i].ident;
                    ClientConnection* client_info = nullptr;
                    
                    {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        for (auto& client : active_clients) {
                            if (client->socket == client_socket) {
                                client_info = client.get();
                                break;
                            }
                        }
                    }
                    
                    if (client_info) {
                        if (!handle_client_data(client_info)) {
                            // Remove from kqueue first
                            struct kevent ev;
                            EV_SET(&ev, client_socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
                            kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
                            
                            disconnect_client(client_socket, client_info);
                            clients_to_remove.push_back(client_socket);
                        }
                    }
                }
            }
#else
            struct epoll_event events[64];
            int nfds = epoll_wait(epoll_fd, events, 64, 1); // 1ms timeout
            
            for (int i = 0; i < nfds; ++i) {
                if (events[i].data.fd == server_socket) {
                    // New connection
                    sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);
                    
                    int client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);
                    if (client_socket >= 0) {
                        if (current_connections.load(std::memory_order_relaxed) >= max_connections) {
                            std::cout << "Async connection rejected: max capacity reached\n";
                            close(client_socket);
                            continue;
                        }
                        
                        // Set non-blocking
                        int flags = fcntl(client_socket, F_GETFL, 0);
                        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
                        
                        char client_ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                        std::string client_address = std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
                        
                        auto conn_id = metrics->connection_started();
                        current_connections.fetch_add(1, std::memory_order_relaxed);
                        
                        std::cout << "Async client connected: " << client_address << " (ID: " << conn_id << ")\n";
                        
                        {
                            std::lock_guard<std::mutex> lock(clients_mutex);
                            auto client = std::make_unique<ClientConnection>(client_socket, client_address, conn_id);
                            active_clients.push_back(std::move(client));
                        }
                        
                        // Add to epoll
                        struct epoll_event ev;
                        ev.events = EPOLLIN;
                        ev.data.fd = client_socket;
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev);
                    }
                } else {
                    // Client data
                    int client_socket = events[i].data.fd;
                    ClientConnection* client_info = nullptr;
                    
                    {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        for (auto& client : active_clients) {
                            if (client->socket == client_socket) {
                                client_info = client.get();
                                break;
                            }
                        }
                    }
                    
                    if (client_info) {
                        if (!handle_client_data(client_info)) {
                            // Remove from epoll first
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, nullptr);
                            
                            disconnect_client(client_socket, client_info);
                            clients_to_remove.push_back(client_socket);
                        }
                    }
                }
            }
#endif
            
            // Clean up disconnected clients outside the event loop
            if (!clients_to_remove.empty()) {
                std::lock_guard<std::mutex> lock(clients_mutex);
                for (int socket_to_remove : clients_to_remove) {
                    active_clients.erase(
                        std::remove_if(active_clients.begin(), active_clients.end(),
                            [socket_to_remove](const std::unique_ptr<ClientConnection>& c) {
                                return c->socket == socket_to_remove;
                            }),
                        active_clients.end());
                }
            }
        }
        
        // Cleanup remaining connections
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& client : active_clients) {
            auto duration = duration_cast<microseconds>(steady_clock::now() - client->start_time);
            metrics->connection_ended(duration, client->message_count, client->total_bytes);
            close(client->socket);
        }
        active_clients.clear();
    }
    
    void stop() {
        running.store(false, std::memory_order_relaxed);
        close(server_socket);
    }
    
    std::shared_ptr<EchoServerMetrics> get_metrics() { return metrics; }
};

// Original threaded echo server
class EchoServer {
private:
    int server_socket;
    std::atomic<bool> running{true};
    std::shared_ptr<EchoServerMetrics> metrics;
    size_t max_connections;
    std::atomic<size_t> current_connections{0};

public:
    EchoServer(const std::string& address, int port, size_t max_conn) 
        : max_connections(max_conn), metrics(std::make_shared<EchoServerMetrics>()) {
        
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr);
        
        if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            throw std::runtime_error("Failed to bind socket");
        }
        
        if (listen(server_socket, SOMAXCONN) < 0) {
            throw std::runtime_error("Failed to listen on socket");
        }
    }
    
    ~EchoServer() {
        close(server_socket);
    }
    
    void handle_client(int client_socket, const std::string& client_addr) {
        auto connection_start = steady_clock::now();
        auto conn_id = metrics->connection_started();
        current_connections.fetch_add(1, std::memory_order_relaxed);
        
        char buffer[1024];
        uint64_t total_bytes = 0;
        size_t message_count = 0;
        
        std::cout << "Client connected: " << client_addr << " (ID: " << conn_id << ")\n";
        
        while (running.load(std::memory_order_relaxed)) {
            ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0) break;
            
            ssize_t bytes_sent = send(client_socket, buffer, bytes_read, 0);
            if (bytes_sent <= 0) break;
            
            total_bytes += bytes_read + bytes_sent;
            message_count++;
        }
        
        auto duration = duration_cast<microseconds>(steady_clock::now() - connection_start);
        metrics->connection_ended(duration, message_count, total_bytes);
        current_connections.fetch_sub(1, std::memory_order_relaxed);
        
        std::cout << "Client disconnected: " << client_addr 
                  << " (ID: " << conn_id 
                  << ", " << duration_cast<milliseconds>(duration).count() << "ms"
                  << ", " << message_count << " messages"
                  << ", " << total_bytes << " bytes)\n";
        
        close(client_socket);
    }
    
    void run() {
        std::cout << "Echo server listening (max " << max_connections << " connections)\n";
        
        while (running.load(std::memory_order_relaxed)) {
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);
            
            int client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);
            if (client_socket < 0) {
                if (running.load(std::memory_order_relaxed)) {
                    std::cerr << "Failed to accept connection\n";
                }
                continue;
            }
            
            if (current_connections.load(std::memory_order_relaxed) >= max_connections) {
                std::cout << "Connection rejected: max capacity reached\n";
                close(client_socket);
                continue;
            }
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            std::string client_address = std::string(client_ip) + ":" + std::to_string(ntohs(client_addr.sin_port));
            
            std::thread(&EchoServer::handle_client, this, client_socket, client_address).detach();
        }
    }
    
    void stop() {
        running.store(false, std::memory_order_relaxed);
        close(server_socket);
    }
    
    std::shared_ptr<EchoServerMetrics> get_metrics() { return metrics; }
};

// Updated echo_client_benchmark to support both sync and async modes
void echo_client_benchmark(const std::string& server_addr, int port, 
                          size_t num_clients, size_t messages_per_client,
                          size_t message_size_kb, bool use_async,
                          std::shared_ptr<AsyncMetrics> metrics) {
    std::cout << "\nECHO CLIENT BENCHMARK (" << (use_async ? "ASYNC" : "THREADED") << ")\n";
    std::cout << "Clients: " << num_clients 
              << ", Messages per client: " << messages_per_client 
              << ", Message size: " << (message_size_kb > 0 ? std::to_string(message_size_kb) : "0") << " KB\n";
    
    std::vector<std::future<void>> futures;
    
    for (size_t client_id = 0; client_id < num_clients; ++client_id) {
        auto spawn_start = steady_clock::now();
        
        auto future = std::async(std::launch::async, [=]() {
            metrics->record_task_spawn(steady_clock::now() - spawn_start);
            
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                std::cerr << (use_async ? "Async " : "") << "Client " << client_id << " failed to create socket\n";
                return;
            }
            
            sockaddr_in server_address{};
            server_address.sin_family = AF_INET;
            server_address.sin_port = htons(port);
            inet_pton(AF_INET, server_addr.c_str(), &server_address.sin_addr);
            
            if (connect(sock, (sockaddr*)&server_address, sizeof(server_address)) < 0) {
                std::cerr << (use_async ? "Async " : "") << "Client " << client_id << " failed to connect\n";
                close(sock);
                return;
            }
            
            // Random generator for message data
            std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<> char_dist('a', 'z');
            
            for (size_t msg_id = 0; msg_id < messages_per_client; ++msg_id) {
                std::string message;
                
                if (message_size_kb > 0) {
                    size_t size = message_size_kb * 1024;
                    message.reserve(size);
                    
                    const size_t chunk_size = 64;
                    for (size_t i = 0; i < size / chunk_size; ++i) {
                        for (size_t j = 0; j < chunk_size; ++j) {
                            message += static_cast<char>(char_dist(rng));
                        }
                    }
                    
                    size_t remaining = size % chunk_size;
                    for (size_t i = 0; i < remaining; ++i) {
                        message += static_cast<char>(char_dist(rng));
                    }
                } else {
                    message = (use_async ? "AsyncClient-" : "Client-") + std::to_string(client_id) + "-Message-" + std::to_string(msg_id);
                }
                
                auto op_start = steady_clock::now();
                
                if (send(sock, message.c_str(), message.size(), 0) < 0) break;
                
                std::vector<char> buffer(message.size(), 0);
                size_t bytes_received = 0;
                while (bytes_received < message.size()) {
                    int result = recv(sock, buffer.data() + bytes_received, 
                                      message.size() - bytes_received, 0);
                    if (result <= 0) break;
                    bytes_received += result;
                }
                
                if (bytes_received < message.size()) break;
                
                metrics->record_async_operation(steady_clock::now() - op_start);
                std::this_thread::sleep_for(milliseconds(1));
            }
            
            std::cout << (use_async ? "Async " : "") << "Client " << client_id << " finished\n";
            
            close(sock);
        });
        
        metrics->record_task_spawn(steady_clock::now() - spawn_start);
        futures.push_back(std::move(future));
    }
    
    for (auto& future : futures) {
        future.wait();
    }
}

void set_thread_high_priority() {
    // pthread_setschedparam could be used here but often requires root privileges
}

// Updated main benchmark function
void echo_server_benchmark(const BenchmarkArgs& args) {
    std::cout << std::string(80, '=') << "\n";
    std::cout << "C++ ECHO SERVER BENCHMARK (" << (args.use_async ? "ASYNC MODE" : "THREADED MODE") << ")\n";
    std::cout << std::string(80, '=') << "\n";
    
    auto metrics = std::make_shared<AsyncMetrics>();
    
    auto cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 4;
    
    size_t thread_count = args.num_threads > 0 ? args.num_threads : cores;
    
    std::cout << "System cores: " << cores << "\n";
    std::cout << "Worker threads: " << thread_count << "\n";
    std::cout << "Concurrency model: " << (args.use_async ? "Async I/O" : "Thread-per-connection") << "\n";
    std::cout << "Benchmark configuration:\n";
    std::cout << "  - Clients: " << args.num_clients << "\n";
    std::cout << "  - Messages per client: " << args.messages_per_client << "\n";
    std::cout << "  - Max connections: " << args.max_connections << "\n";
    std::cout << "  - Message size: " << (args.message_size_kb > 0 ? std::to_string(args.message_size_kb) : "0") << " KB\n";
    
    if (args.use_async) {
        std::cout << "  - Using async I/O with ";
#ifdef __APPLE__
        std::cout << "kqueue";
#else
        std::cout << "epoll";
#endif
        std::cout << "\n";
    }
    
    auto server_metrics = std::make_shared<AsyncMetrics>();
    const std::string server_addr = "127.0.0.1";
    const int port = 9999;
    
    if (args.use_async) {
        // Use async server
        auto async_server = std::make_unique<AsyncEchoServer>(server_addr, port, args.max_connections);
        auto server_future = std::async(std::launch::async, [&async_server]() {
            set_thread_high_priority();
            async_server->run_async();
        });
        
        std::this_thread::sleep_for(milliseconds(100));
        
        echo_client_benchmark(server_addr, port, args.num_clients, 
                              args.messages_per_client, args.message_size_kb, 
                              args.use_async, server_metrics);
        
        async_server->stop();
        server_future.wait();
        
        async_server->get_metrics()->print_metrics("ASYNC");
    } else {
        // Use threaded server
        auto server = std::make_unique<EchoServer>(server_addr, port, args.max_connections);
        auto server_future = std::async(std::launch::async, [&server]() {
            set_thread_high_priority();
            server->run();
        });
        
        std::this_thread::sleep_for(milliseconds(100));
        
        echo_client_benchmark(server_addr, port, args.num_clients, 
                              args.messages_per_client, args.message_size_kb, 
                              args.use_async, server_metrics);
        
        server->stop();
        server_future.wait();
        
        server->get_metrics()->print_metrics("THREADED");
    }
    
    server_metrics->print_metrics(args.use_async ? "Async Echo Server Client" : "Threaded Echo Server Client");
    
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "ECHO SERVER BENCHMARK COMPLETED (" << (args.use_async ? "ASYNC" : "THREADED") << ")\n";
    std::cout << std::string(80, '=') << "\n";
}

int main(int argc, char* argv[]) {
    try {
        BenchmarkArgs args = parse_args(argc, argv);
        
        if (args.use_async) {
            std::cout << "Running in ASYNC mode\n";
#ifdef __APPLE__
            std::cout << "Using kqueue for async I/O\n";
#else
            std::cout << "Using epoll for async I/O\n";
#endif
        } else {
            std::cout << "Running in THREADED mode\n";
        }
        
        echo_server_benchmark(args);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}