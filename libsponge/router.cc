#include "router.hh"

#include "ipv4_header.hh"
#include "util.hh"

#include <cstdint>
#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    _routing_table.push_back({route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    IPv4Header &header = dgram.header();
    if (header.ttl <= 1)
        return;
    header.ttl--;

    // Recompute the header checksum after changing the TTL.
    header.cksum = 0;
    const auto serialized_header = header.serialize();
    InternetChecksum check;
    check.add(serialized_header);
    header.cksum = check.value();

    Route *best_route = nullptr;
    for (auto &route : _routing_table) {
        if (route.prefix_length == 0 || ((header.dst ^ route.route_prefix) >> (32 - route.prefix_length)) == 0) {
            if (best_route == nullptr || route.prefix_length > best_route->prefix_length) {
                best_route = &route;
            }
        }
    }

    if (best_route != nullptr) {
        Address next_hop =
            best_route->next_hop.has_value() ? best_route->next_hop.value() : Address::from_ipv4_numeric(header.dst);
        _interfaces.at(best_route->interface_num).send_datagram(dgram, next_hop);
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
