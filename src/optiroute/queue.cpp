// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include <sstream>
#include <math.h>
#include "datacenter/dynexp_topology.h"
#include "queue.h"
#include "tcppacket.h"
#include "ndppacket.h"
#include "rlbpacket.h" // added
#include "queue_lossless.h"
#include "routing.h"

#include "pipe.h"

#include "rlb.h" // needed to make dummy packet
#include "tcp.h"
#include "rlbmodule.h"

Queue::Queue(linkspeed_bps bitrate, mem_b maxsize, EventList& eventlist, QueueLogger* logger, Routing* routing)
  : EventSource(eventlist,"queue"), _maxsize(maxsize), _tor(0), _port(0), _top(NULL),
    _logger(logger), _bitrate(bitrate), _num_drops(0), _routing(routing)
{
    _queuesize = 0;
    _ps_per_byte = (simtime_picosec)((pow(10.0, 12.0) * 8) / _bitrate);
    _txbytes = 0;
    stringstream ss;
    //ss << "queue(" << bitrate/1000000 << "Mb/s," << maxsize << "bytes)";
    //_nodename = ss.str();
}

Queue::Queue(linkspeed_bps bitrate, mem_b maxsize, EventList& eventlist, QueueLogger* logger, int tor, int port, DynExpTopology *top, Routing* routing)
  : EventSource(eventlist,"queue"), _maxsize(maxsize), _tor(tor), _port(port), _top(top),
    _logger(logger), _bitrate(bitrate), _num_drops(0), _routing(routing)
{
    _queuesize = 0;
    _ps_per_byte = (simtime_picosec)((pow(10.0, 12.0) * 8) / _bitrate);
    stringstream ss;
    _max_recorded_size_slice.resize(_top->get_nlogicslices());
    _max_recorded_size = 0;
    _queue_alarm = new QueueAlarm(eventlist, port, this, top);
    //ss << "queue(" << bitrate/1000000 << "Mb/s," << maxsize << "bytes)";
    //_nodename = ss.str();
}


void Queue::beginService() {
    /* schedule the next dequeue event */
    assert(!_enqueued.empty());
    eventlist().sourceIsPendingRel(*this, drainTime(_enqueued.back()));
    _sending_pkt = _enqueued.back();
}

void Queue::completeService() {
    /* dequeue the packet */
    assert(!_enqueued.empty());
    Packet* pkt = _enqueued.back();
    _enqueued.pop_back();
    _queuesize -= pkt->size();

    _sending_pkt = NULL;
    /* tell the packet to move on to the next pipe */
    //pkt->sendFromQueue();
    sendFromQueue(pkt);

    if (!_enqueued.empty()) {
        /* schedule the next dequeue event */
        beginService();
    }
}

void Queue::sendFromQueue(Packet* pkt) {
    Pipe* nextpipe; // the next packet sink will be a pipe
    DynExpTopology* top = pkt->get_topology();
    if (pkt->get_crthop() < 0) {
        // cout << "sendFromQueue (from NIC) " << _tor << " " << _port << endl; 
        // we're sending out of the NIC
        nextpipe = top->get_pipe_serv_tor(pkt->get_src());
        nextpipe->receivePacket(*pkt);
    } else {
        // cout << "sendFromQueue (from ToR) " << _tor << " " << _port << endl; 
        // we're sending out of a ToR queue
        if (top->is_last_hop(pkt->get_crtport())) {
            pkt->set_lasthop(true);
            // if this port is not connected to _dst, then drop the packet
            if (!top->port_dst_match(pkt->get_crtport(), pkt->get_crtToR(), pkt->get_dst())) {
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
                        cout << "!!! RLB";
                        break;
                    case NDP:
                        cout << "!!! NDP";
                        break;
                    case NDPACK:
                        cout << "!!! NDPACK";
                        break;
                    case NDPNACK:
                        cout << "!!! NDPNACK";
                        break;
                    case NDPPULL:
                        cout << "!!! NDPPULL";
                        break;
                    case TCP: {
                        cout << "!!! TCP";
                        TcpPacket *tcppkt = (TcpPacket*)pkt;
                        tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
                        break;
                    }
                    case TCPACK: {
                        TcpAck *tcpack = (TcpAck*)pkt;
                        cout << "!!! TCPACK efb " << tcpack->early_fb();
                        tcpack->get_tcpsrc()->add_to_dropped(tcpack->seqno());
                    }
                }
                cout << " packet dropped: port & dst didn't match! (queue.cpp)" << endl;
                cout << seqno << "    ToR = " << pkt->get_crtToR() << ", port = " << pkt->get_crtport() <<
                    ", src = " << pkt->get_src() << ", dst = " << pkt->get_dst() << endl;

                pkt->free(); // drop the packet

                return;
            }
        }
        /*
        if(pkt->type() == TCP && pkt->get_src() == 186 && pkt->get_dst() == 121) {
        cout << "SENTOUT " << eventlist().now() << endl;
        }
        */
        nextpipe = top->get_pipe_tor(pkt->get_crtToR(), pkt->get_crtport());
        nextpipe->receivePacket(*pkt);

    }
}

void Queue::doNextEvent() {
    completeService();
}


void Queue::receivePacket(Packet& pkt) {
    if (_queuesize+pkt.size() > _maxsize) {
	/* if the packet doesn't fit in the queue, drop it */
    /*
	if (_logger) 
	    _logger->logQueue(*this, QueueLogger::PKT_DROP, pkt);
	pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_DROP);
    */
	pkt.free();
    //cout << "!!! Packet dropped: queue overflow!" << endl;
    if(pkt.type() == TCP){
        TcpPacket *tcppkt = (TcpPacket*)&pkt;
        tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
    }
    _num_drops++;
	return;
    }
    /* enqueue the packet */
    bool queueWasEmpty = _enqueued.empty();
    _enqueued.push_front(&pkt);
    _queuesize += pkt.size();
    pkt.inc_queueing(_queuesize);

    if (queueWasEmpty) {
	/* schedule the dequeue event */
	assert(_enqueued.size() == 1);
	beginService();
    }
}

//this code is cursed
void Queue::sendEarlyFeedback(Packet *pkt) {
  DynExpTopology* top = pkt->get_topology();

  TcpSrc* tcpsrc = NULL;
  unsigned seqno;
  if(pkt->type() == TCP) {
      tcpsrc = ((TcpPacket*)(pkt))->get_tcpsrc();
      seqno = ((TcpPacket*)(pkt))->seqno();
  } else {
      return;
  }
  assert(tcpsrc != NULL);
  pkt->set_early_fb();
  TcpAck *ack = tcpsrc->alloc_tcp_ack();
  int old_src_ToR = _top->get_firstToR(pkt->get_src());
  // flip the source and dst of the packet:
  int s = pkt->get_src();
  int d = pkt->get_dst();
  ack->set_src(d);
  ack->set_dst(s);
  assert(pkt->get_crtToR() >= 0);
  ack->set_src_ToR(pkt->get_crtToR());
  _routing->routing_from_PQ(ack, eventlist().now());
  ack->set_crtToR(pkt->get_crtToR());
  ack->set_lasthop(false);
  ack->set_hop_index(0);
  ack->set_crthop(0);
  ack->set_early_fb();
  ack->set_int(pkt->get_int());

  // get the current ToR, this will be the new src_ToR of the packet
  int new_src_ToR = ack->get_crtToR();

  if (new_src_ToR == old_src_ToR) {
    // the packet got returned at the source ToR
    // we need to send on a downlink right away
    ack->set_crtport(top->get_lastport(ack->get_dst()));
    ack->set_maxhops(0);
    ack->set_crtslice(0);

  } else {
    ack->set_src_ToR(new_src_ToR);
    _routing->routing_from_ToR(ack, eventlist().now(), eventlist().now()); 
  }
  //assert(!(pkt->get_crtToR() == ack->get_crtToR() && pkt->get_crtToR() && ack->get_crtToR()));
  Queue* nextqueue = top->get_queue_tor(ack->get_crtToR(), ack->get_crtport());
  nextqueue->receivePacket(*ack);
}

mem_b Queue::queuesize() {
    return _queuesize;
}

simtime_picosec Queue::serviceTime() {
    return _queuesize * _ps_per_byte;
}

void Queue::reportQueuesize() {
    cout << "Queue " << _tor << " " << _port << " " << queuesize() << endl;
}

void Queue::reportMaxqueuesize(){
    cout << "Queue " << _tor << " " << _port << " " << _max_recorded_size << endl;
    _max_recorded_size = 0;
}

//format "tor,port,slice0max,slice1max,slice2max...,slicenmax"
void Queue::reportMaxqueuesize_perslice(){
    cout << _tor << "," << _port << ",";
    for(int i = 0; i < _max_recorded_size_slice.size(); i++) {
        cout << _max_recorded_size_slice[i] << ",";
        _max_recorded_size_slice[i] = 0;
    }
    cout << endl;
}

simtime_picosec Queue::get_queueing_delay(int slice){
    return slice_queuesize(slice)*_ps_per_byte;
}

//////////////////////////////////////////////////
//              Priority Queue                  //
//////////////////////////////////////////////////

PriorityQueue::PriorityQueue(linkspeed_bps bitrate, mem_b maxsize, 
			     EventList& eventlist, QueueLogger* logger, int node, DynExpTopology *top, Routing* routing)
    : Queue(bitrate, maxsize, eventlist, logger, routing)
{
    _node = node;
    _top = top;

    _bytes_sent = 0;

    _queuesize[Q_RLB] = 0;
    _queuesize[Q_LO] = 0;
    _queuesize[Q_MID] = 0;
    _queuesize[Q_HI] = 0;
    _servicing = Q_NONE;
    //_state_send = LosslessQueue::READY;
}

PriorityQueue::queue_priority_t PriorityQueue::getPriority(Packet& pkt) {
    queue_priority_t prio = Q_LO;
    switch (pkt.type()) {
    case TCPACK:
    case NDPACK:
    case NDPNACK:
    case NDPPULL:
    case NDPLITEACK:
    case NDPLITERTS:
    case NDPLITEPULL:
        prio = Q_HI;
        break;
    case NDP:
        if (pkt.header_only()) {
            prio = Q_HI;
        } else {
        /*
            NdpPacket* np = (NdpPacket*)(&pkt);
            if (np->retransmitted()) {
                prio = Q_MID;
            } else {
                prio = Q_LO;
            }
        */
        }
        break;
    case RLB:
        prio = Q_RLB;
        break;
    case TCP:
    case IP:
    case NDPLITE:
        prio = Q_LO;
        break;
    default:
        cout << "NIC couldn't identify packet type." << endl;
        abort();
    }
    
    return prio;
}

simtime_picosec PriorityQueue::serviceTime(Packet& pkt) {
    queue_priority_t prio = getPriority(pkt);
    switch (prio) {
    case Q_LO:
	   //cout << "q_lo: " << _queuesize[Q_HI] + _queuesize[Q_MID] + _queuesize[Q_LO] << " ";
	   return (_queuesize[Q_HI] + _queuesize[Q_MID] + _queuesize[Q_LO]) * _ps_per_byte;
    case Q_MID:
	   //cout << "q_mid: " << _queuesize[Q_MID] + _queuesize[Q_LO] << " ";
	   return (_queuesize[Q_HI] + _queuesize[Q_MID]) * _ps_per_byte;
    case Q_HI:
	   //cout << "q_hi: " << _queuesize[Q_LO] << " ";
	   return _queuesize[Q_HI] * _ps_per_byte;
    case Q_RLB:
        abort(); // we should never check this for an RLB packet
    default:
	   abort();
    }
}

void PriorityQueue::doorbell(bool rlbwaiting) {

    if (rlbwaiting) { // add a dummy packet to the queue

        // debug:
        //cout << "NIC[node" << _node << "] - doorbell that RLB has packets" << endl;

        //if (_node == 345 && timeAsUs(eventlist().now()) > 18100) {
    	//	cout << "   Doorbell TRUE at " << timeAsUs(eventlist().now()) << " us." << endl;
    	//}


        RlbPacket* pkt = RlbPacket::newpkt(1500); // make a dummy packet
        pkt->set_dummy(true);
        receivePacket(*pkt); // put that dummy packet in the RLB queue
            // ! note - use `receivePacket` so we trigger service to begin
    } else {
        // the RLB module isn't ready to send more packets right now
        // drop the dummy packet in the queue so we don't try to pull from the RLB module

        //if (_node == 345 && timeAsUs(eventlist().now()) > 18100) {
    	//	cout << "   Doorbell FALSE at " << timeAsUs(eventlist().now()) << " us." << endl;
    	//}

        Packet* pkt = _queue[0].front(); // RLB enumerates to 0
        pkt->free();
        _queue[0].pop_front();
        _queuesize[0] = 0; // set queuesize to zero. 
    }
}

void PriorityQueue::receivePacket(Packet& pkt) {
    //cout << "PrioQueue receivePacket " << pkt.get_src() << " " << pkt.get_dst() << " size:" << pkt.size() << endl;

    queue_priority_t prio = getPriority(pkt);

    // debug:
    //cout << "   NIC received a packet." << endl;

    //pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_ARRIVE);
    /*
    if(queuesize()+pkt.size() > _maxsize) {
	pkt.free();
	if(pkt.type() == TCP){
		TcpPacket *tcppkt = (TcpPacket*)&pkt;
		tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
	}
	cout << "DROPPED AT NIC\n";
	return;
    }
    */

    /* enqueue the packet */
    bool queueWasEmpty = false;
    if (queuesize() == 0)
        queueWasEmpty = true;

    _queuesize[prio] += pkt.size();
    _queue[prio].push_front(&pkt);

    //if (_logger)
    //    _logger->logQueue(*this, QueueLogger::PKT_ENQUEUE, pkt);

    if (queueWasEmpty) { // && _state_send==LosslessQueue::READY) {
        /* schedule the dequeue event */
        assert(_queue[Q_RLB].size() + _queue[Q_LO].size() + _queue[Q_MID].size() + _queue[Q_HI].size() == 1);
        beginService();
    }
}

void PriorityQueue::beginService() {
    //assert(_state_send == LosslessQueue::READY);

    /* schedule the next dequeue event */
    for (int prio = Q_HI; prio >= Q_RLB; --prio) {
        if (_queuesize[prio] > 0) {
            int slice = _top->time_to_absolute_slice(eventlist().now());
            // if(slice % 3 != 0) {
                // eventlist().sourceIsPendingRel(*this, _top->get_slice_start_time(slice + (3 - slice % 3)) - eventlist().now() + drainTime(_queue[prio].back()));
            // } else {
                eventlist().sourceIsPendingRel(*this, drainTime(_queue[prio].back()));
            // }
            // cout << "PrioQueue (node " << _nodename << ") sending a packet at " << timeAsUs(eventlist().now()) << " us" << endl;
            // cout << "   will be drained in " << timeAsUs(drainTime(_queue[prio].back())) << " us" << endl;

            _servicing = (queue_priority_t)prio;

            // debug:
            //if (_node == 345 && timeAsUs(eventlist().now()) > 18100) {
    		//	cout << "   beginService on _servicing: [" << _servicing << "] at " << timeAsUs(eventlist().now()) << " us." << endl;
    		//}

            return;
        }
    }
}

void PriorityQueue::completeService() {
	if (!_queue[_servicing].empty()) {

	    Packet* pkt;

	    switch (_servicing) {
	    case Q_RLB:
	    {
            //cout << "RLB PQ completeService\n";
	        RlbModule* mod = _top->get_rlb_module(_node);
	        pkt = mod->NICpull(); // get the packet from the RLB module

	        // check if the packet is a dummy (spacer packet for rate limiting)
	        // If so, free the packet, and return;
	        if (pkt->is_dummy()) {
	            pkt->free();
	            break;
	        } else {
	            // RLB only applies to packets between racks.
                // for RLB, we actually set `slice_sent` when it's committed by the RLB module, not when it's sent by the NIC

                pkt->set_src_ToR(_top->get_firstToR(pkt->get_src())); // set the sending ToR. This is used for subsequent routing

	            // send on the first path (index 0) to the "intermediate" destination
	            int path_index = 0; // index 0 ensures it's the direct path
	            pkt->set_path_index(path_index); // set which path the packet will take

	            // set some initial packet parameters used for routing
	            pkt->set_lasthop(false);
	            pkt->set_crthop(-1);
	            pkt->set_crtToR(-1);
		    //cout << "debug " << pkt->get_src_ToR() << " " << pkt->get_dst() << " " << pkt->get_slice_sent() << endl;
            if(pkt->get_src_ToR() == _top->get_firstToR(pkt->get_dst())) {
                cout << "flow " << pkt->get_src() << " " << pkt->get_dst() << endl;
            }
	            pkt->set_maxhops(_top->get_no_hops(pkt->get_src_ToR(),
	                _top->get_firstToR(pkt->get_dst()), pkt->get_slice_sent(), path_index));
	        }
	        /* tell the packet to move on to the next pipe */
	        sendFromQueue(pkt);

	        break;
	    }
	    case Q_LO:
	    case Q_MID:
	    case Q_HI:
	    {

	        pkt = _queue[_servicing].back(); // get the pointer to the packet
	        _queue[_servicing].pop_back(); // delete the element of the queue
	        _queuesize[_servicing] -= pkt->size(); // decrement the queue size

	        int new_bytes_sent = _bytes_sent + pkt->size();
	        if (new_bytes_sent / 1500 > _bytes_sent / 1500) {
	            // we sent a "full" packet ahead of RLB, notify RLB to push
	            RlbModule* mod = _top->get_rlb_module(_node);
	            mod->NICpush();
	        }
	        _bytes_sent = new_bytes_sent;

	        // set the routing info
            pkt->set_src_ToR(_top->get_firstToR(pkt->get_src())); // set the sending ToR. This is used for subsequent routing

            _routing->routing_from_PQ(pkt, eventlist().now());
	        /* tell the packet to move on to the next pipe */
	        sendFromQueue(pkt);

	        break;
	    }
	    case Q_NONE:
	    	break;
	        //abort();
	    }
	}

    if (queuesize() > 0) {
        beginService();
    } else {
        _servicing = Q_NONE;
    }
}

mem_b PriorityQueue::queuesize() {
    return _queuesize[Q_RLB] + _queuesize[Q_LO] + _queuesize[Q_MID] + _queuesize[Q_HI];
}

mem_b PriorityQueue::slice_queuesize(int slice){
	//unimplemented 
    assert(0);
    return 0;
}

