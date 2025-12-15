#pragma once

#ifdef USE_DPDK
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#endif

#include <cstdint>
#include <vector>
#include <functional>

namespace hft::network {

    class DPDKPoller {
    public:
        DPDKPoller(uint16_t port_id = 0);
        ~DPDKPoller();

        // Initialize EAL and Port
        // Returns number of arguments consumed
        int init(int argc, char** argv);
        
        // Start the device
        void start();
        
        // Poll for packets (burst read)
        // Callback receives pointer to packet data and length
        void poll(std::function<void(const uint8_t*, uint16_t)> callback);

        // Send a packet (burst write)
        void send(const uint8_t* data, uint16_t len);

    private:
#ifdef USE_DPDK
        uint16_t port_id_;
        struct rte_mempool *mbuf_pool_;
        struct rte_eth_conf port_conf_;
#endif
    };

}
