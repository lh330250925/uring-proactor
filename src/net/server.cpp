#include "net/server.hpp"
#include "core/tcp_thread.hpp"
#include "core/udp_thread.hpp"
#include <numa.h>
#include <sched.h>
#include <stdexcept>

static std::vector<int> get_cpu_list()
{
    std::vector<int> cpus;
    int num_nodes = numa_num_configured_nodes();
    if (num_nodes > 0)
    {
        bitmask *mask = numa_allocate_cpumask();
        for (int node = 0; node < num_nodes; ++node)
        {
            numa_node_to_cpus(node, mask);
            for (int cpu = 0; cpu < (int)mask->size; ++cpu)
                if (numa_bitmask_isbitset(mask, cpu))
                    cpus.push_back(cpu);
        }
        numa_free_cpumask(mask);
    }
    if (cpus.empty())
    {
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(0, sizeof(set), &set) == 0)
            for (int i = 0; i < CPU_SETSIZE; ++i)
                if (CPU_ISSET(i, &set))
                    cpus.push_back(i);
    }
    return cpus;
}

TcpServer::TcpServer(int port, int thread_nums, int buf_pool_size,
                     int channel_capacity, int buf_ring_size, int pool_size,
                     unsigned queue_depth, bool cpu_affinity)
    : Server(port, thread_nums, buf_pool_size, cpu_affinity),
      channel_capacity_(channel_capacity),
      buf_ring_size_(buf_ring_size),
      pool_size_(pool_size),
      queue_depth_(queue_depth)
{
    threads_.reserve(thread_nums);
}

TcpServer::~TcpServer() = default;

void TcpServer::start()
{
    std::vector<int> cpu_list;
    if (cpu_affinity_)
    {
        cpu_list = get_cpu_list();
        if ((int)cpu_list.size() < get_thread_nums() * 2)
            throw std::runtime_error("not enough CPUs for cpu_affinity mode");
    }
    for (int i = 0; i < get_thread_nums(); ++i)
    {
        int cpu_id = cpu_affinity_ ? cpu_list[i * 2] : -1;
        threads_.emplace_back(std::make_unique<TcpThread>(*this, cpu_id));
    }
}

UdpServer::~UdpServer() = default;

UdpServer::UdpServer(int port, int thread_nums, int buf_pool_size,
                     int buf_ring_size, int msghdr_pool_size, int channel_capacity,
                     unsigned queue_depth, bool cpu_affinity)
    : Server(port, thread_nums, buf_pool_size, cpu_affinity),
      buf_ring_size_(buf_ring_size),
      msghdr_pool_size_(msghdr_pool_size),
      channel_capacity_(channel_capacity),
      queue_depth_(queue_depth)
{
    threads_.reserve(thread_nums);
}

void UdpServer::start()
{
    std::vector<int> cpu_list;
    if (cpu_affinity_)
    {
        cpu_list = get_cpu_list();
        if ((int)cpu_list.size() < get_thread_nums() * 2)
            throw std::runtime_error("not enough CPUs for cpu_affinity mode");
    }
    for (int i = 0; i < get_thread_nums(); ++i)
    {
        int cpu_id = cpu_affinity_ ? cpu_list[i * 2] : -1;
        threads_.emplace_back(std::make_unique<UdpThread>(*this, cpu_id));
    }
}