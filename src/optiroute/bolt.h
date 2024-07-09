// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef BOLT_H
#define BOLT_H

/*
 * A Bolt source simply changes the congestion control algorithm.
 */

#include "config.h"
#include "tcp.h"
#include "tcppacket.h"

class BoltSrc : public TcpSrc {
 public:
     BoltSrc(TcpLogger* logger, TrafficLogger* pktlogger, EventList &eventlist, 
             DynExpTopology *top, int flow_src, int flow_dst, Routing *routing);
    ~BoltSrc(){}

    // Mechanism
#ifdef TDTCP
    virtual void deflate_window(int slice);
#else
    virtual void deflate_window();
#endif
    virtual void receivePacket(Packet& pkt);
    virtual void rtx_timer_hook(simtime_picosec now,simtime_picosec period);
    virtual void startflow();
    virtual void cleanup();
    virtual void doNextEvent();

 private:
    uint32_t _past_cwnd;
    uint32_t _crtwnd;
    uint32_t _cwnd;
    unsigned _last_sequpdate;
    simtime_picosec _latest_ts = 0;

    map<string, pktINT> _link_ints;
    double _base_rtt;
    double _nic_rate;
    double _bdp;
    double _U;
    unsigned _last_seq_ai;
    simtime_picosec _last_dec_t;
    bool _is_new_slice;

    void handleSRC(TcpAck *pkt);
    void handleAck(TcpAck *pkt);
};

#endif
