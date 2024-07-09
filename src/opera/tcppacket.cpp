#include "tcppacket.h"

PacketDB<TcpPacket> TcpPacket::_packetdb;
PacketDB<TcpAck> TcpAck::_packetdb;
PacketDB<SamplePacket> SamplePacket::_packetdb;
