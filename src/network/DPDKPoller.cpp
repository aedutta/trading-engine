#include "network/DPDKPoller.hpp"
#include <iostream>

namespace hft::network {

    DPDKPoller::DPDKPoller(uint16_t port_id) 
#ifdef USE_DPDK
        : port_id_(port_id), mbuf_pool_(nullptr) 
#endif
    {
#ifdef USE_DPDK
        // Default port configuration
        // In a real HFT app, you would tune rxmode (e.g., RSS, Offloads)
        port_conf_ = {};
        port_conf_.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;
#endif
    }

    DPDKPoller::~DPDKPoller() {
#ifdef USE_DPDK
        // Cleanup if needed
#endif
    }

    int DPDKPoller::init(int argc, char** argv) {
#ifdef USE_DPDK
        // 1. Initialize EAL
        int ret = rte_eal_init(argc, argv);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
        }

        // 2. Create mbuf pool
        // 8191 elements, 250 cache size, 0 priv size, default data room size
        mbuf_pool_ = rte_pktmbuf_pool_create("MBUF_POOL", 8191, 250, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
        if (mbuf_pool_ == nullptr) {
            rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
        }

        // Check if any ports are available
        uint16_t nb_ports = rte_eth_dev_count_avail();
        if (nb_ports == 0) {
            std::cout << "[DPDK] WARNING: No DPDK-compatible ports found. Network ingestion will be disabled." << std::endl;
            // Do NOT exit, just return success so the rest of the engine can run
            return 0; 
        }

        // 3. Configure port
        uint16_t nb_rx_q = 1;
        uint16_t nb_tx_q = 1;
        struct rte_eth_dev_info dev_info;
        
        ret = rte_eth_dev_info_get(port_id_, &dev_info);
        if (ret != 0) {
            rte_exit(EXIT_FAILURE, "Error getting device info\n");
        }
        
        ret = rte_eth_dev_configure(port_id_, nb_rx_q, nb_tx_q, &port_conf_);
        if (ret != 0) {
            rte_exit(EXIT_FAILURE, "Error configuring device\n");
        }

        // 4. Setup RX Queue
        ret = rte_eth_rx_queue_setup(port_id_, 0, 1024, rte_eth_dev_socket_id(port_id_), NULL, mbuf_pool_);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, "Error setting up RX queue\n");
        }
        
        // 5. Setup TX Queue (even if we only read, we might need it)
        ret = rte_eth_tx_queue_setup(port_id_, 0, 1024, rte_eth_dev_socket_id(port_id_), NULL);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, "Error setting up TX queue\n");
        }

        return ret;
#else
        std::cout << "[DPDK] Not enabled. Skipping init." << std::endl;
        return 0;
#endif
    }

    void DPDKPoller::start() {
#ifdef USE_DPDK
        int retval = rte_eth_dev_start(port_id_);
        if (retval < 0) {
             rte_exit(EXIT_FAILURE, "Error starting device\n");
        }
        rte_eth_promiscuous_enable(port_id_); 
        std::cout << "[DPDK] Port " << port_id_ << " started." << std::endl;
#endif
    }

    void DPDKPoller::poll(std::function<void(const uint8_t*, uint16_t)> callback) {
#ifdef USE_DPDK
        struct rte_mbuf *bufs[32];
        // Burst read from the NIC
        const uint16_t nb_rx = rte_eth_rx_burst(port_id_, 0, bufs, 32);

        for (int i = 0; i < nb_rx; i++) {
            // Get pointer to packet data
            uint8_t* pkt_data = rte_pktmbuf_mtod(bufs[i], uint8_t*);
            
            // Parse Ethernet Header
            struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)pkt_data;
            
            // Check for IPv4
            if (eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
                struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
                
                // Check for UDP
                if (ip_hdr->next_proto_id == IPPROTO_UDP) {
                    // Calculate payload offset: Eth + IP + UDP
                    // Note: IP header length is in 32-bit words
                    size_t ip_hdr_len = (ip_hdr->version_ihl & 0x0F) * 4;
                    struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)((uint8_t *)ip_hdr + ip_hdr_len);
                    
                    // Payload follows UDP header
                    uint8_t *payload = (uint8_t *)(udp_hdr + 1);
                    
                    // Calculate payload length
                    // UDP length includes header (8 bytes)
                    uint16_t udp_len = rte_be_to_cpu_16(udp_hdr->dgram_len);
                    uint16_t payload_len = udp_len - sizeof(struct rte_udp_hdr);
                    
                    // Invoke callback with payload only
                    callback(payload, payload_len);
                }
            }
            
            // Free the buffer back to the pool
            rte_pktmbuf_free(bufs[i]);
        }
#endif
    }

    void DPDKPoller::send(const uint8_t* data, uint16_t len) {
#ifdef USE_DPDK
        struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool_);
        if (unlikely(mbuf == nullptr)) {
            return;
        }

        // 1. Prepend Headers (Ethernet + IP + UDP)
        // For this demo, we will just construct a dummy Ethernet frame to ensure it leaves the NIC.
        // In a real HFT system, you would use a pre-calculated template (Zero-Copy).
        
        size_t header_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
        size_t total_len = header_len + len;

        // Append data to mbuf
        char *payload = rte_pktmbuf_append(mbuf, total_len);
        if (unlikely(payload == nullptr)) {
            rte_pktmbuf_free(mbuf);
            return;
        }

        // Fill Headers (Dummy values for benchmark)
        struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)payload;
        eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
        
        // Copy payload after headers
        rte_memcpy(payload + header_len, data, len);

        // Send
        uint16_t nb_tx = rte_eth_tx_burst(port_id_, 0, &mbuf, 1);
        
        // Free if not sent
        if (unlikely(nb_tx < 1)) {
            rte_pktmbuf_free(mbuf);
        }
#endif
    }

}
