#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

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
    const uint32_t ip = next_hop.ipv4_numeric();

    auto iter = arpMap.find(ip);
    if (iter == arpMap.end()) {
        arpMap[ip] = ArpEntry();
        arpMap[ip].wait.push_back(dgram);
        return;
    }
    auto &entry = iter->second;
    if (entry.valid() && entry.expiration >= time) {
        EthernetFrame frame;
        auto &header = frame.header();
        header.type = EthernetHeader::TYPE_IPv4;
        header.dst = entry.addr;
        header.src = _ethernet_address;

        frame.payload() = dgram.serialize();

        _frames_out.push(frame);
        return;
    }
    entry.wait.push_back(dgram);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const auto &header = frame.header();
    if (header.type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram ipv4;
        if (ipv4.parse(frame.payload()) != ParseResult::NoError) {
            throw runtime_error("Parse Error");
        }
        return ipv4;
    }
    if (header.type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp;
        if (arp.parse(frame.payload()) != ParseResult::NoError) {
            throw runtime_error("Parse Error");
        }

        auto iter = arpMap.find(arp.sender_ip_address);
        if (iter == arpMap.end()) {
            arpMap[arp.sender_ip_address] = ArpEntry(arp.sender_ethernet_address, time + PERIOD);
        } else {
            auto &entry = iter->second;
            entry.addr = arp.sender_ethernet_address;
            entry.expiration = time + PERIOD;
            if (!entry.wait.empty()) {
                for (auto &i : entry.wait) {
                    EthernetFrame frame;
                    auto &header = frame.header();
                    header.type = EthernetHeader::TYPE_IPv4;
                    header.dst = arp.sender_ethernet_address;
                    header.src = _ethernet_address;

                    frame.payload() = i.serialize();

                    _frames_out.push(frame);
                }
                entry.wait.clear();
            }
        }

        if (arp.opcode == arp.OPCODE_REQUEST && arp.target_ip_address == _ip_address.ipv4_numeric()) {
            EthernetFrame frame;
            auto &header = frame.header();
            header.type = EthernetHeader::TYPE_ARP;
            header.dst = arp.sender_ethernet_address;
            header.src = _ethernet_address;

            arp.opcode = arp.OPCODE_REPLY;
            arp.target_ip_address = arp.sender_ip_address;
            arp.target_ethernet_address = arp.sender_ethernet_address;
            arp.sender_ip_address = _ip_address.ipv4_numeric();
            arp.sender_ethernet_address = _ethernet_address;

            frame.payload() = arp.serialize();

            _frames_out.push(frame);
        }
        return {};
    }
    throw runtime_error("Error Type");
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { time += ms_since_last_tick; }
