#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <cstdint>
#include <iostream>
#include <utility>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    auto it = _arp_cache.find(next_hop_ip);

    // in cache, make frame and send IPV4
    if (it != _arp_cache.end() and it->second.mac.has_value()) {
        EthernetFrame frame(it->second.mac.value(), _ethernet_address, EthernetHeader::TYPE_IPv4, dgram.serialize());
        _frames_out.push(frame);
        return;
    }

    // not in cache, send ARP
    if (it == _arp_cache.end()) {
        ArpEntry &entry = _arp_cache[next_hop_ip];
        entry.ttl = 5000;

        ARPMessage arp_request(
            ARPMessage::OPCODE_REQUEST, _ethernet_address, _ip_address.ipv4_numeric(), {}, next_hop_ip);

        EthernetFrame arp_frame(
            ETHERNET_BROADCAST, _ethernet_address, EthernetHeader::TYPE_ARP, arp_request.serialize());
        _frames_out.push(arp_frame);
    }

    // re-populate ArpEntry because unordered_map may have rehashed on insert
    ArpEntry &entry = _arp_cache[next_hop_ip];
    entry.waiting.push(dgram);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return {};
    }

    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) == ParseResult::NoError) {
            return dgram;
        }
    }

    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage msg;
        if (msg.parse(frame.payload()) == ParseResult::NoError) {
            EthernetAddress sender_mac = msg.sender_ethernet_address;
            uint32_t sender_ip = msg.sender_ip_address;
            ArpEntry &entry = _arp_cache[sender_ip];
            entry.mac = sender_mac;
            entry.ttl = 30000;

            if (msg.opcode == ARPMessage::OPCODE_REQUEST && msg.target_ip_address == _ip_address.ipv4_numeric()) {
                ARPMessage arp_reply(ARPMessage::OPCODE_REPLY,
                                     _ethernet_address,
                                     _ip_address.ipv4_numeric(),
                                     msg.sender_ethernet_address,
                                     msg.sender_ip_address);

                EthernetFrame reply_frame(
                    msg.sender_ethernet_address, _ethernet_address, EthernetHeader::TYPE_ARP, arp_reply.serialize());
                _frames_out.push(reply_frame);
            } else if (msg.opcode == ARPMessage::OPCODE_REPLY) {
                IPv4Datagram dgram;
                while (!entry.waiting.empty()) {
                    dgram = entry.waiting.front();
                    entry.waiting.pop();
                    EthernetFrame out_frame(
                        entry.mac.value(), _ethernet_address, EthernetHeader::TYPE_IPv4, dgram.serialize());
                    _frames_out.push(out_frame);
                }
            }
        }
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    auto it = _arp_cache.begin();
    while (it != _arp_cache.end()) {
        ArpEntry &entry = it->second;
        if (entry.ttl > ms_since_last_tick) {
            entry.ttl -= ms_since_last_tick;
            it++;
            continue;
        }

        it = _arp_cache.erase(it);
    }
}
