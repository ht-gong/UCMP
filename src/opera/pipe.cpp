// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "pipe.h"
#include <iostream>
#include <sstream>

#include "queue.h"
#include "tcp.h"
#include "ndp.h"
//#include "rlb.h"
#include "rlbmodule.h"
#include "ndppacket.h"
#include "rlbpacket.h" // added for debugging

#define LINKRATE 100000000000
#define REPORTING_PERIOD 1 // outputs whenever this sampling limit is reached

Pipe::Pipe(simtime_picosec delay, EventList& eventlist)
: EventSource(eventlist,"pipe"), _delay(delay)
{
    //stringstream ss;
    //ss << "pipe(" << delay/1000000 << "us)";
    //_nodename= ss.str();

    _bytes_delivered = 0;
    _bytes_passed_through = 0;

}

void Pipe::receivePacket(Packet& pkt)
{
    //pkt.flow().logTraffic(pkt,*this,TrafficLogger::PKT_ARRIVE);
    //cout << "Pipe: Packet received at " << timeAsUs(eventlist().now()) << " us" << endl;
    if (_inflight.empty()){
        /* no packets currently inflight; need to notify the eventlist
            we've an event pending */
        eventlist().sourceIsPendingRel(*this,_delay);
    }
    _inflight.push_front(make_pair(eventlist().now() + _delay, &pkt));
}

void Pipe::doNextEvent() {
    if (_inflight.size() == 0) 
        return;

    Packet *pkt = _inflight.back().second;
    _inflight.pop_back();
    //pkt->flow().logTraffic(*pkt, *this,TrafficLogger::PKT_DEPART);

    // tell the packet to move itself on to the next hop
    //pkt->sendOn();
    //pkt->sendFromPipe();
    sendFromPipe(pkt);

    if (!_inflight.empty()) {
        // notify the eventlist we've another event pending
        simtime_picosec nexteventtime = _inflight.back().first;
        _eventlist.sourceIsPending(*this, nexteventtime);
    }
}

uint64_t Pipe::reportBytes() {
    uint64_t temp;
    temp = _bytes_delivered;
    _bytes_delivered = 0; // reset the counter
    return temp;
}

uint64_t Pipe::reportBytesPassedThrough() {
    uint64_t temp;
    temp = _bytes_passed_through;
    _bytes_passed_through = 0; // reset the counter
    return temp;
}

void Pipe::sendFromPipe(Packet *pkt) {
    //cout << "sendFromPipe" << endl;
    _bytes_passed_through += pkt->size();
    if (pkt->is_lasthop()) {
        //cout << "LAST HOP\n";
        // we'll be delivering to an NdpSink or NdpSrc based on packet type
        // ! OR an RlbModule
        switch (pkt->type()) {
            case RLB:
                {
                    // check if it's really the last hop for this packet
                    // otherwise, it's getting indirected, and doesn't count.
                    if (pkt->get_dst() == pkt->get_real_dst()) {

                        _bytes_delivered = _bytes_delivered + pkt->size(); // increment packet delivered

                        // debug:
                        //cout << "!!! incremented packets delivered !!!" << endl;

                    }

                    DynExpTopology* top = pkt->get_topology();
		    pkt->inc_crthop();
                    RlbModule * mod = top->get_rlb_module(pkt->get_dst());
                    assert(mod);
                    mod->receivePacket(*pkt, 0);
                    break;
                }
            case NDP:
                {
                    if (pkt->bounced() == false) {

                        //NdpPacket* ndp_pkt = dynamic_cast<NdpPacket*>(pkt);
                        //if (!ndp_pkt->retransmitted())
                        if (pkt->size() > 64) // not a header
                            _bytes_delivered = _bytes_delivered + pkt->size(); // increment packet delivered

                        // send it to the sink
                        NdpSink* sink = pkt->get_ndpsink();
                        assert(sink);
                        sink->receivePacket(*pkt);
                    } else {
                        // send it to the source
                        NdpSrc* src = pkt->get_ndpsrc();
                        assert(src);
                        src->receivePacket(*pkt);
                    }

                    break;
                }
            case TCP:
                {
                    // send it to the sink
                    _bytes_delivered += pkt->size();
                    TcpSink *sink = pkt->get_tcpsink();
                    assert(sink);
                    sink->receivePacket(*pkt);
                    break;
                }
            case TCPACK:
                {
                    TcpSrc* src = pkt->get_tcpsrc();
                    assert(src);
                    src->receivePacket(*pkt);
                    break;
                }
            case NDPACK:
            case NDPNACK:
            case NDPPULL:
                {
                    NdpSrc* src = pkt->get_ndpsrc();
                    assert(src);
                    src->receivePacket(*pkt);
                    break;
                }
        }
    } else {
        // we'll be delivering to a ToR queue
        DynExpTopology* top = pkt->get_topology();

        if (pkt->get_crtToR() < 0)
            pkt->set_crtToR(pkt->get_src_ToR());

        else {
            // this was assuming the topology didn't change:
            //pkt->set_crtToR(top->get_nextToR(pkt->get_slice_sent(), pkt->get_crtToR(), pkt->get_crtport()));

            // under a changing topology, we have to use the current slice:
            // we compute the current slice at the end of the packet transmission
            // !!! as a future optimization, we could have the "wrong" host NACK any packets
            // that go through at the wrong time
            
            // Relaxing the constraint so packets will reach their destination if it has been pushed onto the wire 
            // just before reconfig. It will arrive at the dst of its sending slice.
	    simtime_picosec crt_t = eventlist().now() - _delay;
            int64_t superslice = (crt_t / top->get_slicetime(3)) %
                top->get_nsuperslice();
            // next, get the relative time from the beginning of that superslice
            int64_t reltime = crt_t - superslice*top->get_slicetime(3) -
                (crt_t / (top->get_nsuperslice()*top->get_slicetime(3))) * 
                (top->get_nsuperslice()*top->get_slicetime(3));
            int slice; // the current slice
            if (reltime < top->get_slicetime(0))
                slice = 0 + superslice*3;
            else if (reltime < top->get_slicetime(0) + top->get_slicetime(1))
                slice = 1 + superslice*3;
            else
                slice = 2 + superslice*3;

            int nextToR = top->get_nextToR(slice, pkt->get_crtToR(), pkt->get_crtport());
            if (nextToR >= 0) {// the rotor switch is up
                pkt->set_crtToR(nextToR);
            } else { // the rotor switch is down, "drop" the packet
                unsigned seqno = 0;
                if(pkt->type() == TCP) {
                    seqno = ((TcpPacket*)pkt)->seqno();
                } else if (pkt->type() == TCPACK) {
                    seqno = ((TcpAck*)pkt)->ackno();
                } else if (pkt->type() == NDP) {
                    seqno = ((NdpPacket*)pkt)->seqno();
                }
                
                switch (pkt->type()) {
                    
                    case RLB:
                        {
                            // for now, let's just return the packet rather than implementing the RLB NACK mechanism
                            RlbPacket *p = (RlbPacket*)(pkt);
                            RlbModule* module = top->get_rlb_module(p->get_src()); // returns pointer to Rlb module that sent the packet
                            module->receivePacket(*p, 1); // 1 means to put it at the front of the queue
                            break;
                        }
                    case NDP:
                        cout << "!!! NDP packet clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        cout << "    current slice = " << slice << endl;
                        cout << "    slice sent = " << pkt->get_slice_sent() << endl;
                        cout << "    src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;
                        pkt->free();
                        break;
                    case NDPACK:
                        cout << "!!! NDP ACK clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        cout << "    current slice = " << slice << endl;
                        cout << "    slice sent = " << pkt->get_slice_sent() << endl;
                        cout << "    src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;
                        pkt->free();
                        break;
                    case NDPNACK:
                        cout << "!!! NDP NACK clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        cout << "    current slice = " << slice << endl;
                        cout << "    slice sent = " << pkt->get_slice_sent() << endl;
                        cout << "    src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;
                        pkt->free();
                        break;
                    case NDPPULL:
                        cout << "!!! NDP PULL clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        cout << "    current slice = " << slice << endl;
                        cout << "    slice sent = " << pkt->get_slice_sent() << endl;
                        cout << "    src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;
                        pkt->free();
                        break;
                    case TCP:
                        cout << "!!! TCP packet clipped in pipe (rotor switch down)" << endl;
                        cout << "    time = " << timeAsUs(eventlist().now()) << " us";
                        cout << "    current slice = " << slice << endl;
                        cout << "    slice sent = " << pkt->get_slice_sent() << endl;
                        cout << "    src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;
                        TcpPacket *tcppkt = (TcpPacket*)pkt;
                        tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
                        pkt->free();
                        break;
                }
                return;
            }
        }
        pkt->inc_crthop(); // increment the hop

        // get the port:
        if (pkt->get_crthop() == pkt->get_maxhops()) { // no more hops defined, need to set downlink port
            pkt->set_crtport(top->get_lastport(pkt->get_dst()));

        } else {
            pkt->set_crtport(top->get_port(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()),
                        pkt->get_slice_sent(), pkt->get_path_index(), pkt->get_crthop()));
        }

        Queue* nextqueue = top->get_queue_tor(pkt->get_crtToR(), pkt->get_crtport());
        assert(nextqueue);
        nextqueue->receivePacket(*pkt);
    }
}


//////////////////////////////////////////////
//      Aggregate utilization monitor       //
//////////////////////////////////////////////


UtilMonitor::UtilMonitor(DynExpTopology* top, EventList &eventlist)
  : EventSource(eventlist,"utilmonitor"), _top(top)
{
    _H = _top->no_of_nodes(); // number of hosts
    _N = _top->no_of_tors(); // number of racks
    _hpr = _top->no_of_hpr(); // number of hosts per rack
    
    uint64_t rate = LINKRATE / 8; // bytes / second
    rate = rate * _H;
    //rate = rate / 1500; // total packets per second

    _max_agg_Bps = rate;
    _counter = 0;

    // debug:
    //cout << "max bytes per second = " << rate << endl;

}

void UtilMonitor::start(simtime_picosec period) {
    _period = period;
    _max_B_in_period = _max_agg_Bps * timeAsSec(_period);

    // debug:
    //cout << "_max_pkts_in_period = " << _max_pkts_in_period << endl;
    printAggUtil();
    //eventlist().sourceIsPending(*this, _period);
}

void UtilMonitor::doNextEvent() {
    printAggUtil();
}

void UtilMonitor::printAggUtil() {
    if (_counter % REPORTING_PERIOD == 0) {
        uint64_t B_downlink_sum = 0;
        uint64_t B_uplink_sum = 0;
    
        for (int tor = 0; tor < _N; tor++) {
            for (int downlink = 0; downlink < _hpr; downlink++) {
                Pipe* pipe = _top->get_pipe_tor(tor, downlink);
                uint64_t bytes = pipe->reportBytes();
                B_downlink_sum += bytes;
                cout << "Pipe " <<  tor << " " << downlink << " " << (double) bytes / (LINKRATE / 8 * timeAsSec(_period) * REPORTING_PERIOD)  << endl;
            }
            for (int uplink = 0; uplink < _hpr; uplink++) {
                Pipe* pipe = _top->get_pipe_tor(tor, _hpr + uplink);
                uint64_t bytes = pipe->reportBytesPassedThrough();
                B_uplink_sum += bytes;
                cout << "Pipe " <<  tor << " " << _hpr + uplink 
                     << " " << (double) bytes / (LINKRATE / 8 * timeAsSec(_period) * REPORTING_PERIOD)  << endl;
            }
        }

        // debug:
        //cout << "Packets counted = " << (int)pkt_sum << endl;
        //cout << "Max packets = " << _max_pkts_in_period << endl;
        double util_downlink = (double)B_downlink_sum / ((double)_max_B_in_period * REPORTING_PERIOD);
        double util_uplink = (double)B_uplink_sum / ((double)_max_B_in_period * REPORTING_PERIOD);
        assert(util_downlink <= 1.0 && util_uplink <= 1.0);
        cout << "Util " << fixed << util_downlink << " " << util_uplink << " " << timeAsMs(eventlist().now()) << endl;

        for (int tor = 0; tor < _top->no_of_tors(); tor++) {
            for (int uplink = 0; uplink < _top->no_of_hpr(); uplink++) {
                Queue* q = _top->get_queue_tor(tor, _top->no_of_hpr() + uplink);
                q->reportQueuesize();
            }
        }
     }
    
    _counter++;
    // cout << "QueueReport" << endl;
    // for (int tor = 0; tor < _top->no_of_tors(); tor++) {
    //     for (int uplink = _top->no_of_hpr()+1; uplink < _top->no_of_hpr()*2; uplink++) {
    //         Queue* q = _top->get_queue_tor(tor, uplink);
    //         q->reportMaxqueuesize();
    //     }
    // }

    if (eventlist().now() + _period < eventlist().getEndtime())
        eventlist().sourceIsPendingRel(*this, _period);

}
