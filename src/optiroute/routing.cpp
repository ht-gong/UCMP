// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "routing.h"
#include <math.h>

#include <climits>
#include <iostream>
#include <sstream>
#include <algorithm>

#include "config.h"
#include "dynexp_topology.h"
#include "network.h"
#include "tcppacket.h"
#include "tcp.h"
#include "ndp.h"
// #define DEBUG
//#define HOHO

//#define DYNAMIC_FSIZE

// #define REROUTE_MITIGATION
#define LOOKUP

#define PUSHTIME_PER_PKT 620000
#define DOWNGRADE_POWER 2

extern uint32_t delay_host2ToR; // nanoseconds, host-to-tor link
extern uint32_t delay_ToR2ToR; // nanoseconds, tor-to-tor link


double Routing::get_pkt_priority(TcpSrc* tcp_src) {
    if(_routing_algorithm == LONGSHORT) {
        if(tcp_src->get_flowsize() < _cutoff) {
            return 0.0;
        } else {
            return 1.0;
        }
    } else if (_routing_algorithm == OPTIROUTE) {
	if(_options & ROUTING_OPT_AGING) {
	    return (double) tcp_src->get_flowsize()-tcp_src->get_remaining_flowsize();
	} else if (_options & ROUTING_OPT_SRTF) {
            return (double) tcp_src->get_remaining_flowsize();
	} else if (_options & ROUTING_OPT_ALPHA0) {
            return 0.0;
	} else {
	    return (double) tcp_src->get_flowsize();
	}
    }
    return 0.0;
}

double Routing::get_pkt_priority(NdpSrc* ndp_src) {
    if(_routing_algorithm == LONGSHORT) {
        if(ndp_src->get_flowsize() < _cutoff) {
            return 0.0;
        } else {
            return 1.0;
        }
    } else if (_routing_algorithm == OPTIROUTE) {
        #ifdef DYNAMIC_FSIZE
            return (double) ndp_src->get_remaining_flowsize();
        #endif
	if(_options & ROUTING_OPT_AGING) {
	    return (double) ndp_src->get_flowsize()-ndp_src->get_remaining_flowsize();
	} else {
	    return (double) ndp_src->get_flowsize();
	}
    }
    return 0.0;
}

// Routing out of Host into Tor
simtime_picosec Routing::routing_from_PQ(Packet* pkt, simtime_picosec t) {
    DynExpTopology* top = pkt->get_topology();
    // initial setup
    pkt->set_lasthop(false);
    pkt->set_crthop(-1);
    pkt->set_hop_index(-1);
    pkt->set_crtToR(-1);

     
    if (pkt->get_src_ToR() == top->get_firstToR(pkt->get_dst())) {
        // the packet is being sent within the same rack
    } else {// the packet is being sent between racks

        // we will choose the path based on the current slice
        int slice = top->time_to_logic_slice(t);
        
        pkt->set_slice_sent(slice); // "timestamp" the packet
        pkt->set_fabricts(t);
        pkt->set_path_index(get_path_index(pkt, t)); // set which path the packet will take
        if(_routing_algorithm == OPTIROUTE) {
            DynExpTopology* _top = pkt->get_topology();
            pkt->set_planned_hops(_top->get_no_hops(pkt->get_src_ToR(),
	                _top->get_firstToR(pkt->get_dst()), pkt->get_slice_sent(), pkt->get_path_index()));
        }
        pkt->set_crtslice(slice);
    }

    return 0;
}

simtime_picosec Routing::routing_from_ToR(Packet* pkt, simtime_picosec t, simtime_picosec init_time) {
	DynExpTopology* top = pkt->get_topology();
    // if this is the last ToR, need to get downlink port
    if(pkt->get_crtToR() == top->get_firstToR(pkt->get_dst())){
        pkt->set_crtport(top->get_lastport(pkt->get_dst()));
        pkt->set_crtslice(0);
        return 0;
    }

    // dispatches to different routing strategies
    if(_routing_algorithm == VLB || (_routing_algorithm == LONGSHORT && pkt->get_priority() == 1.0)) {
        return routing_from_ToR_VLB(pkt, t, t);
    } else if(_routing_algorithm == OPTIROUTE) {
        if(pkt->type() == RLB) {
            int cur_slice = top->time_to_logic_slice(t);
            pkt->set_crtport(top->get_port(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()),
                        pkt->get_slice_sent(), pkt->get_path_index(), pkt->get_crthop()));
            pkt->set_crtslice(cur_slice);
            return 0;
        }
        return routing_from_ToR_OptiRoute(pkt, t, t, false);
    } else {
        return routing_from_ToR_Expander(pkt, t, t, false);
    }
}

int Routing::get_path_index(Packet* pkt, simtime_picosec t) {
    DynExpTopology* top = pkt->get_topology();
    if(_routing_algorithm == SINGLESHORTEST) {
        return pkt->get_path_index();
    }
    if(_routing_algorithm == KSHORTEST) {
        int physical_slice = top->time_to_slice(t);
        return pkt->get_topology()->get_rpath_indices(pkt->get_src(), pkt->get_dst(), physical_slice);
    }
    // special case since the number of paths may change as we go across slices
    if(_routing_algorithm == ECMP) {
        int physical_slice = top->time_to_slice(t);
        // perserve original outgoing port and adds it onto new src_Tor to get path index
        int newsrc = pkt->get_src_ToR() * top->no_of_hpr() + pkt->get_src() % top->no_of_hpr();
        return top->get_rpath_indices(newsrc, pkt->get_dst(), physical_slice);
    }
    if(_routing_algorithm == OPTIROUTE) {
        int logical_slice = top->time_to_logic_slice(t);
        #ifdef REROUTE_MITIGATION
            int maxhop = top->get_path_max_hop();
            // if(top->is_reconfig(t, 500000)){ 
            //     if(pkt->get_crthop() > maxhop) {
            //         cout<<"downgraded\n";
            //         index = std::max(index - 1, 1);
            //     }
            // }
            if(pkt->get_crthop() >= maxhop && top->is_reconfig(t, PUSHTIME_PER_PKT * maxhop)) {
                pkt->set_priority(pkt->get_priority() * DOWNGRADE_POWER);
            }
        #endif 
        vector<pair<uint64_t, vector<int>>>* v = top->get_lb_and_paths(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()), logical_slice);
        assert(v->size());
        int index = 1;
        #ifndef HOHO
        while(index < v->size()) {
            if((double) (*v)[index].first > pkt->get_priority()) {
                break;
            }
            index++;
        }
        #endif
        return top->get_path_indices(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()), pkt->get_src(), pkt->get_dst(), logical_slice, index - 1);
    }
    return 0;
}

simtime_picosec Routing::routing_from_ToR_Expander(Packet* pkt, simtime_picosec t, simtime_picosec init_time, bool rerouted) {
    unsigned seqno = 0;
    if(pkt->type() == TCP) {
        seqno = ((TcpPacket*)pkt)->seqno();
    } else if (pkt->type() == TCPACK) {
        seqno = ((TcpAck*)pkt)->ackno();
    } else if (pkt->type() == NDP) {
        seqno = ((NdpPacket*)pkt)->seqno();
    }

    DynExpTopology* top = pkt->get_topology();

    int cur_slice = top->time_to_logic_slice(t);
    if(cur_slice != pkt->get_crtslice()) {
    pkt->set_src_ToR(pkt->get_crtToR());
    pkt->set_path_index(get_path_index(pkt, t));
    pkt->set_hop_index(0);
  }
    // next port assuming topology does not change
	int cur_slice_port = top->get_port(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()),
                                cur_slice, pkt->get_path_index(), pkt->get_hop_index());
    if(!cur_slice_port) cout << "err " << pkt->get_src_ToR() << " " << top->get_firstToR((pkt->get_dst())) << " " <<
    cur_slice << " " << pkt->get_path_index() << " " << pkt->get_hop_index() << endl;
    //assert(cur_slice_port);
    if(!cur_slice_port || cur_slice_port > 11) {
        std::cout <<pkt->id() << " routing failed crtToR: " << pkt->get_crtToR() << " Srctor:" <<pkt->get_src_ToR() << " dstToR: " << top->get_firstToR(pkt->get_dst()) << " slice: " << cur_slice 
        << " hop: " << pkt->get_hop_index() << " port: "<<cur_slice_port<< 
      " pathidx " << pkt->get_path_index() << " efb " << pkt->early_fb() << " is_data " << (pkt->type() == TCP) << endl;
        assert(0);
    }

    Queue* cur_q = top->get_queue_tor(pkt->get_crtToR(), cur_slice_port);
    
    simtime_picosec finish_push;
    // to get accurate queueing delay, need to add from when the queue began servicing
    if(cur_q->get_is_servicing() && init_time == t) { // only needed when init_slice == cur_slice
        finish_push = cur_q->get_last_service_time() + cur_q->get_queueing_delay(cur_slice) +
            cur_q->drainTime(pkt);
    } else {
        finish_push = t + cur_q->get_queueing_delay(cur_slice) +
            cur_q->drainTime(pkt);
    }

    int finish_push_slice = top->time_to_logic_slice(finish_push); // plus the link delay

    // calculate delay considering the queue occupancy
    simtime_picosec finish_time = finish_push + timeFromNs(delay_ToR2ToR);
    int finish_slice = top->time_to_logic_slice(finish_time);

    #ifdef DEBUG
    cout <<seqno << "Routing Exp crtToR: " << pkt->get_crtToR() << "Srctor:" <<pkt->get_src_ToR() << " dstToR: " << top->get_firstToR(pkt->get_dst()) << " priority: " << pkt->get_priority() 
            << " hop: " << pkt->get_hop_index() << " port: "<<cur_slice_port<< " Now: " << t << " Logsl: " << top->time_to_logic_slice(t) << " Physl: " << top->time_to_slice(t)  
            << " Last comp: "<< cur_q->get_last_service_time() << " Finish push:" << finish_push_slice << " " << finish_push << " Finish slice:"<<finish_slice<< " " << finish_time << endl;
    #endif
    if(top->is_reconfig(finish_push)) {
        finish_push_slice = top->absolute_logic_slice_to_slice(finish_push_slice + 1);
    }

    // traversed full cycle, cannot find space to allocate packet
    if(top->time_to_absolute_logic_slice(t - init_time) + 1 == top->get_nlogicslices()) {
        pkt->set_size(UINT16_MAX);  // dummy size to tell queue to drop packet
        pkt->set_crtslice(cur_slice);
        pkt->set_crtport(cur_slice_port);
        return 0;
    }

    // 3 possibilities:
    if (finish_push_slice == cur_slice) {
        // 1. The packet is pushed and propogated within the same slice
        pkt->set_crtslice(cur_slice);
        pkt->set_crtport(cur_slice_port);
        if(finish_slice != cur_slice && top->get_nextToR(cur_slice, pkt->get_crtToR(), pkt->get_crtport()) != top->get_firstToR(pkt->get_dst())) {
            // 2. The packet is pushed in the same slice, but finishes propogating in the next
            pkt->set_src_ToR(top->get_nextToR(cur_slice, pkt->get_crtToR(), pkt->get_crtport()));
            pkt->set_path_index(get_path_index(pkt, finish_time));
            pkt->set_hop_index(-1);
          if(pkt->id() == 2979155) {
        cout << "DEBUG fail routing\n";
        }
          if(pkt->id() == 2980467) {
        cout << "DEBUG EFB fail routing\n";
        }
        }
          if(pkt->id() == 2979155) {
        cout << "DEBUG routing " << cur_slice << " " << cur_slice_port << endl;
        }
          if(pkt->id() == 2980467) {
        cout << "DEBUG EFB routing " << cur_slice << " " << cur_slice_port << endl;
        }
        return finish_time;
    } else {
        // 3. The packet is pushed "across slices", thus it needs to be rerouted to the start of next slice
        
        simtime_picosec nxt_slice_time = top->get_logic_slice_start_time(top->time_to_absolute_logic_slice(t) + 1);
        pkt->set_src_ToR(pkt->get_crtToR());
        pkt->set_path_index(get_path_index(pkt, nxt_slice_time));
        pkt->set_hop_index(0);
        #ifdef DEBUG
        cout << "Reroute Exp crtToR: " << pkt->get_crtToR() << " dstToR: " << top->get_firstToR(pkt->get_dst()) << " slice: " << pkt->get_crtslice()<< " at " <<t << "finishing at" << finish_push << endl;
        #endif
        return routing_from_ToR_Expander(pkt, nxt_slice_time, init_time, true);
    }

}

simtime_picosec Routing::routing_from_ToR_OptiRoute(Packet* pkt, simtime_picosec t, simtime_picosec init_time, bool rerouted) {
    unsigned seqno = 0;
    if(pkt->type() == TCP) {
        seqno = ((TcpPacket*)pkt)->seqno();
    } else if (pkt->type() == TCPACK) {
        seqno = ((TcpAck*)pkt)->ackno();
    } else if (pkt->type() == NDP) {
        seqno = ((NdpPacket*)pkt)->seqno();
    }

    DynExpTopology* top = pkt->get_topology();

    int cur_slice = top->time_to_logic_slice(t);
    // next port assuming topology does not change
    #ifdef LOOKUP
	int expected_port = top->get_port(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()),
                                pkt->get_slice_sent(), pkt->get_path_index(), pkt->get_hop_index());
    int expected_slice = top->get_opt_slice(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()),
                                pkt->get_slice_sent(), pkt->get_path_index(), pkt->get_hop_index());
    #endif

    if(top->time_to_absolute_logic_slice(t - init_time) > top->get_nlogicslices()) {
        pkt->set_crtslice(expected_slice);
        pkt->set_crtport(expected_port);
        return 0;
    }

    #ifdef STRICT
	int expected_port = top->get_port(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()),
                                pkt->get_slice_sent(), pkt->get_path_index(), pkt->get_hop_index());
    int expected_slice = top->get_opt_slice(pkt->get_src_ToR(), top->get_firstToR(pkt->get_dst()),
                                pkt->get_slice_sent(), pkt->get_path_index(), pkt->get_hop_index()); 
    #endif

    assert(expected_port);
    #ifdef DEBUG
    if(!expected_port) {
        std::cout <<seqno << "Routing Failed crtToR: " << pkt->get_crtToR() << "Srctor:" <<pkt->get_src_ToR() << " dstToR: " << top->get_firstToR(pkt->get_dst()) << " slice: " << cur_slice 
        << "hop: " << pkt->get_hop_index() << "port: "<<expected_port<< endl;
        assert(0);
    }
    #endif

    Queue* cur_q = top->get_queue_tor(pkt->get_crtToR(), expected_port);
    
    simtime_picosec finish_push;
    // to get accurate queueing delay, need to add from when the queue began servicing
    if(cur_q->get_is_servicing() && top->time_to_logic_slice(init_time) == cur_slice) { // only needed when init_slice == cur_slice
        finish_push = cur_q->get_last_service_time() + cur_q->get_queueing_delay(cur_slice) +
            cur_q->drainTime(pkt);
    } else {
        finish_push = t + cur_q->get_queueing_delay(cur_slice) +
            cur_q->drainTime(pkt);
    }

    int finish_push_slice = top->time_to_logic_slice(finish_push); // plus the link delay

    // calculate delay considering the queue occupancy
    simtime_picosec finish_time = finish_push + timeFromNs(delay_ToR2ToR);
    int finish_slice = top->time_to_logic_slice(finish_time);

    #ifdef DEBUG
    cout <<seqno << "Routing Opt crtToR: " << pkt->get_crtToR() << "Srctor:" <<pkt->get_src_ToR() << " dstToR: " << top->get_firstToR(pkt->get_dst()) << " priority: " << pkt->get_priority() 
            << " hop: " << pkt->get_hop_index() << " Expectedport: "<<expected_port<< " Now: " << t << " Logsl: " << top->time_to_logic_slice(t) << " Physl: " << top->time_to_slice(t)  
            << " Sentsl: " <<pkt->get_slice_sent() << " Expectedsl: " << expected_slice << " Finish push:" << finish_push_slice << " " << finish_push << " Finish slice:"<<finish_slice<< " " << finish_time << endl;
    #endif
    if(top->is_reconfig(finish_push)) {
        finish_push_slice = top->absolute_logic_slice_to_slice(finish_push_slice + 1);
    }

    #ifdef LOOKUP
    // 2 possibilities:
    if (finish_push_slice == expected_slice) {
        // 1. The packet is pushed within the expected slice
        pkt->set_crtslice(expected_slice);
        pkt->set_crtport(expected_port);
        return finish_time;
    } else {
        // 2. The packet cannot match current schedule, thus we start rerouting it from the current slice
        simtime_picosec nxt_slice_time;
        if(!rerouted) {
            // Never been rerouted before, start from now
            nxt_slice_time = t;
        } else {
            nxt_slice_time = top->get_logic_slice_start_time(top->time_to_absolute_logic_slice(t) + 1);
        }
        pkt->set_src_ToR(pkt->get_crtToR());
        pkt->set_path_index(get_path_index(pkt, nxt_slice_time));
        pkt->set_slice_sent(top->time_to_logic_slice(nxt_slice_time));
        pkt->set_hop_index(0);
        #ifdef DEBUG
        cout << seqno <<"Reroute Opt crtToR: " << pkt->get_crtToR() << " dstToR: " << top->get_firstToR(pkt->get_dst()) << " slice: " << pkt->get_crtslice()<< " at " <<t <<  " next route time " << nxt_slice_time << endl;
        #endif
        return routing_from_ToR_OptiRoute(pkt, nxt_slice_time, init_time, true);
    }
    #endif 

    #ifdef STRICT
    pkt->set_crtslice(expected_slice);
    pkt->set_crtport(expected_port);
    #endif

}

simtime_picosec Routing::routing_from_ToR_VLB(Packet* pkt, simtime_picosec t, simtime_picosec init_time) {
    unsigned seqno = 0;
    if(pkt->type() == TCP) {
        seqno = ((TcpPacket*)pkt)->seqno();
    } else if (pkt->type() == TCPACK) {
        seqno = ((TcpAck*)pkt)->ackno();
    } else if (pkt->type() == NDP) {
        seqno = ((NdpPacket*)pkt)->seqno();
    }

    DynExpTopology* top = pkt->get_topology();
    assert(top->get_nslices() == top->get_nlogicslices()); 
    // Following code assumes no. logical_slice == no. physical_slice

    int cur_slice = top->time_to_slice(t);

	assert(pkt->get_hop_index() == 1 || pkt->get_hop_index() == 0);
    pair<int, int> route = top->get_direct_routing(pkt->get_crtToR(), 
                                                    top->get_firstToR(pkt->get_dst()), cur_slice);
    if(pkt->get_hop_index() == 1 || route.second == cur_slice) {
        pkt->set_crtport(route.first);
        pkt->set_crtslice(route.second);
    } else {
        int randnum = top->get_rpath_indices(pkt->get_src(), pkt->get_dst(), top->time_to_slice(init_time));
        pkt->set_crtport(top->no_of_hpr() + randnum);
        pkt->set_crtslice(cur_slice);
    }
    Queue* cur_q = top->get_queue_tor(pkt->get_crtToR(), pkt->get_crtport());	
    simtime_picosec finish_push;	
    // to get accurate queueing delay, need to add from when the queue began servicing	
    if(cur_q->get_is_servicing() && top->time_to_slice(init_time) == cur_slice) { // only needed when init_slice == cur_slice	
        finish_push = cur_q->get_last_service_time() + cur_q->get_queueing_delay(cur_slice) +	
            cur_q->drainTime(pkt);	
    } else {	
        finish_push = t + cur_q->get_queueing_delay(pkt->get_crtslice()) +	
            cur_q->drainTime(pkt);	
    }	

    #ifdef DEBUG
    simtime_picosec finish_time = finish_push + timeFromNs(delay_ToR2ToR);
    int finish_slice = top->time_to_slice(finish_time);
    int finish_push_slice = top->time_to_slice(finish_push);
    if(top->is_reconfig(finish_push)) {
        finish_push_slice = top->absolute_slice_to_slice(finish_push_slice + 1);
    }
    cout <<seqno << "Routing VLB crtToR: " << pkt->get_crtToR() << " Srctor:" <<pkt->get_src_ToR() << " dstToR: " << top->get_firstToR(pkt->get_dst()) << " slice: " << cur_slice 
            << " hop: " << pkt->get_hop_index() << " port: "<<pkt->get_crtport()<< " queuedslice: "<<pkt->get_crtslice()<< " currtime: " << t <<" Finish push:" << finish_push_slice << " " << finish_push << " Finish slice:"<<finish_slice<< " " << finish_time << endl;
    #endif

    // if(top->time_to_slice(finish_push) != pkt->get_crtslice()) { // if we cannot send out packet in original planned slice, route again
    //     // Does not follow VLB faithfully, but other options are drop packet/make it stay in calendarq -> which cause RTOs
    //     return routing_from_ToR_VLB(pkt, top->get_slice_start_time(top->time_to_absolute_slice(t) + 1), init_time);	
    // }

    return finish_push;
}

QueueAlarm::QueueAlarm(EventList &eventlist, int port, Queue* q, DynExpTopology* top)
    : EventSource(eventlist, "QueueAlarm"), _queue(q), _top(top)
{
    //nic or downlink queue uses no alarm, sending slice for packet must be set to 0 in that case
    if (!top || top->is_downlink(port)){
        return;
    }
    simtime_picosec t = eventlist.now();
    int next_absolute_slice = top->time_to_absolute_slice(t) + 1;
    simtime_picosec next_time = top->get_logic_slice_start_time(next_absolute_slice);
    assert(next_time > t);
    eventlist.sourceIsPending(*this, next_time); 
}

void QueueAlarm::doNextEvent(){
    simtime_picosec t = eventlist().now();
    int crt = _top->time_to_logic_slice(t);
#ifdef CALENDAR_CLEAR
    //wrap around modulo
    int prev = ((crt-1)+_top->get_nsuperslice()*2)%(_top->get_nsuperslice()*2);
    //clear previous slice calendar queue
    _queue->clearSliceQueue(prev);
#endif

    if(_queue->slice_queuesize(_queue->_crt_tx_slice) > 0) {
        std::cout << "Packets " << _queue->slice_queuesize(_queue->_crt_tx_slice)/1436 << 
            " stuck in queue tor " << _queue->_tor << " port " << _queue->_port << " slice " << _queue->_crt_tx_slice << endl;
	_queue->handleStuck();
    }

    // cout << "QueueAlarmUtil" << fixed << " " << timeAsMs(eventlist().now()) << endl;
    // _queue->reportQueuesize();

    assert(_queue->_crt_tx_slice != crt);
    _queue->_crt_tx_slice = crt;
    #ifdef DEBUG
    cout << "queue slice " << crt << " " << _queue->_tor << "SETTING TO " << crt << endl;
    #endif
    if(!_queue->isTxing() && _queue->slice_queuesize(crt) > 0) {
        #ifdef DEBUG
        cout << "queue slice " << crt << " " << _queue->_tor << " alarm beginService quesize: " <<_queue->queuesize() << endl;
        #endif
        _queue->beginService();
    }
    int next_absolute_slice = _top->time_to_absolute_logic_slice(t) + 1;
    simtime_picosec next_time = _top->get_logic_slice_start_time(next_absolute_slice);
    assert(next_time > t);
    eventlist().sourceIsPending(*this, next_time); 
}
