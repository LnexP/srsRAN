/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srslte/common/mac_pcap_net.h"

namespace srslte {

mac_pcap_net::mac_pcap_net() : mac_pcap_base() {}

mac_pcap_net::~mac_pcap_net()
{
  close();
}
uint32_t mac_pcap_net::open(std::string client_ip_addr_,
                            std::string bind_addr_str,
                            uint16_t    client_udp_port_,
                            uint16_t    bind_udp_port_,
                            uint32_t    ue_id_)
{
  std::lock_guard<std::mutex> lock(mutex);

  if (socket.is_init()) {
    logger.error("PCAP socket writer for %s already running. Close first.", bind_addr_str.c_str());
    return SRSLTE_ERROR;
  }

  if (not socket.open_socket(
          net_utils::addr_family::ipv4, net_utils::socket_type::datagram, net_utils::protocol_type::UDP)) {
    logger.error("Couldn't open socket %s to write PCAP", bind_addr_str.c_str());
    return SRSLTE_ERROR;
  }
  if (not socket.bind_addr(bind_addr_str.c_str(), bind_udp_port_)) {
    socket.reset();
    logger.error("Couldn't bind socket %s to write PCAP", bind_addr_str.c_str());
    return SRSLTE_ERROR;
  }

  logger.info("Sending MAC PCAP frames to %s:%d (from %s:%d)",
              client_ip_addr_.c_str(),
              client_udp_port_,
              bind_addr_str.c_str(),
              bind_udp_port_);
  client_addr.sin_family      = AF_INET;
  client_addr.sin_addr.s_addr = inet_addr(client_ip_addr_.c_str());
  client_addr.sin_port        = htons(client_udp_port_);
  running                     = true;
  ue_id                       = ue_id_;
  // start writer thread
  start();

  return SRSLTE_SUCCESS;
}

uint32_t mac_pcap_net::close()
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (running == false || socket.is_init() == false) {
      return SRSLTE_ERROR;
    }

    // tell writer thread to stop
    running        = false;
    pcap_pdu_t pdu = {};
    queue.push(std::move(pdu));
  }

  wait_thread_finish();
  // close socket handle
  if (socket.is_init()) {
    std::lock_guard<std::mutex> lock(mutex);
    socket.close();
  }

  return SRSLTE_SUCCESS;
}

void mac_pcap_net::write_pdu(pcap_pdu_t& pdu)
{
  if (pdu.pdu != nullptr && socket.is_init()) {
    switch (pdu.rat) {
      case srslte_rat_t::lte:
        write_mac_lte_pdu_to_net(pdu);
        break;
      case srslte_rat_t::nr:
        write_mac_nr_pdu_to_net(pdu);
        break;
      default:
        logger.error("Error writing PDU to PCAP socket. Unsupported RAT selected.");
    }
  }
}

void mac_pcap_net::write_mac_lte_pdu_to_net(pcap_pdu_t& pdu)
{
  int      bytes_sent;
  uint32_t offset = 0;
  uint8_t  buffer[PCAP_CONTEXT_HEADER_MAX];

  // MAC_LTE_START_STRING for UDP heuristics
  memcpy(buffer + offset, MAC_LTE_START_STRING, strlen(MAC_LTE_START_STRING));
  offset += strlen(MAC_LTE_START_STRING);

  offset += LTE_PCAP_PACK_MAC_CONTEXT_TO_BUFFER(&pdu.context, buffer + offset, PCAP_CONTEXT_HEADER_MAX);

  if (pdu.pdu.get()->get_headroom() < offset) {
    logger.error("PDU headroom is to small for adding context buffer");
    return;
  }

  pdu.pdu.get()->msg -= offset;
  memcpy(pdu.pdu.get()->msg, buffer, offset);
  pdu.pdu.get()->N_bytes += offset;

  bytes_sent = sendto(socket.get_socket(),
                      pdu.pdu.get()->msg,
                      pdu.pdu.get()->N_bytes,
                      0,
                      (const struct sockaddr*)&client_addr,
                      sizeof(client_addr));

  if ((int)pdu.pdu.get()->N_bytes != bytes_sent || bytes_sent < 0) {
    logger.error(
        "Sending UDP packet mismatches %d != %d (err %s)", pdu.pdu.get()->N_bytes, bytes_sent, strerror(errno));
  }
}

void mac_pcap_net::write_mac_nr_pdu_to_net(pcap_pdu_t& pdu)
{
  int      bytes_sent;
  uint32_t offset = 0;
  uint8_t  buffer[PCAP_CONTEXT_HEADER_MAX];

  // MAC_LTE_START_STRING for UDP heuristics
  memcpy(buffer + offset, MAC_LTE_START_STRING, strlen(MAC_LTE_START_STRING));
  offset += strlen(MAC_LTE_START_STRING);

  offset += NR_PCAP_PACK_MAC_CONTEXT_TO_BUFFER(&pdu.context_nr, buffer + offset, PCAP_CONTEXT_HEADER_MAX);

  if (pdu.pdu.get()->get_headroom() < offset) {
    logger.error("PDU headroom is to small for adding context buffer");
    return;
  }

  pdu.pdu.get()->msg -= offset;
  memcpy(pdu.pdu.get()->msg, buffer, offset);
  pdu.pdu.get()->N_bytes += offset;

  bytes_sent = sendto(socket.get_socket(),
                      pdu.pdu.get()->msg,
                      pdu.pdu.get()->N_bytes,
                      0,
                      (const struct sockaddr*)&client_addr,
                      sizeof(client_addr));

  if ((int)pdu.pdu.get()->N_bytes != bytes_sent || bytes_sent < 0) {
    logger.error(
        "Sending UDP packet mismatches %d != %d (err %s)", pdu.pdu.get()->N_bytes, bytes_sent, strerror(errno));
  }
}
} // namespace srslte