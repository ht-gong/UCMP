// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include <sstream>
#include <math.h>
#include "datacenter/dynexp_topology.h"
#include "queue.h"
#include "tcppacket.h"
#include "ndppacket.h"
#include "rlbpacket.h" // added
#include "queue_lossless.h"

#include "pipe.h"

#include "rlb.h" // needed to make dummy packet
#include "tcp.h"
#include "rlbmodule.h"

Queue::Queue(linkspeed_bps bitrate, mem_b maxsize, EventList& eventlist, QueueLogger* logger)
  : EventSource(eventlist,"queue"), _maxsize(maxsize), _tor(0), _port(0), _top(NULL),
    _logger(logger), _bitrate(bitrate), _num_drops(0)
{
    _queuesize = 0;
    _ps_per_byte = (simtime_picosec)((pow(10.0, 12.0) * 8) / _bitrate);
    stringstream ss;
    //ss << "queue(" << bitrate/1000000 << "Mb/s," << maxsize << "bytes)";
    //_nodename = ss.str();
}

Queue::Queue(linkspeed_bps bitrate, mem_b maxsize, EventList& eventlist, QueueLogger* logger, int tor, int port, DynExpTopology *top)
  : EventSource(eventlist,"queue"), _maxsize(maxsize), _tor(tor), _port(port), _top(top),
    _logger(logger), _bitrate(bitrate), _num_drops(0)
{
    _queuesize = 0;
    _ps_per_byte = (simtime_picosec)((pow(10.0, 12.0) * 8) / _bitrate);
    stringstream ss;
    _max_recorded_size_slice.resize(_top->get_nsuperslice());
    _max_recorded_size = 0;
    //ss << "queue(" << bitrate/1000000 << "Mb/s," << maxsize << "bytes)";
    //_nodename = ss.str();
}


void Queue::beginService() {
    /* schedule the next dequeue event */
    assert(!_enqueued.empty());
    eventlist().sourceIsPendingRel(*this, drainTime(_enqueued.back()));
}

void Queue::completeService() {
    /* dequeue the packet */
    assert(!_enqueued.empty());
    Packet* pkt = _enqueued.back();
    _enqueued.pop_back();
    _queuesize -= pkt->size();

    /* tell the packet to move on to the next pipe */
    //pkt->sendFromQueue();
    sendFromQueue(pkt);

    if (!_enqueued.empty()) {
        /* schedule the next dequeue event */
        beginService();
    }
}

void Queue::sendFromQueue(Packet* pkt) {
    updatePktOut(pkt->flow_id());
    Pipe* nextpipe; // the next packet sink will be a pipe
    DynExpTopology* top = pkt->get_topology();
    if (pkt->get_crthop() < 0) {
        //cout << "sendFromQueue (from NIC) " << _tor << " " << _port << endl; 
        // we're sending out of the NIC
        nextpipe = top->get_pipe_serv_tor(pkt->get_src());
        nextpipe->receivePacket(*pkt);
    } else {
        pkt->add_hop(_tor);
        //cout << "sendFromQueue (from ToR) " << _tor << " " << _port << endl; 
        // we're sending out of a ToR queue
        if (top->is_last_hop(pkt->get_crtport())) {
            pkt->set_lasthop(true);
            // if this port is not connected to _dst, then drop the packet
            if (!top->port_dst_match(pkt->get_crtport(), pkt->get_crtToR(), pkt->get_dst())) {

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
                    case TCP:
                        cout << "!!! TCP";
                        TcpPacket *tcppkt = (TcpPacket*)pkt;
                        tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
                }
                cout << " packet dropped: port & dst didn't match! (queue.cpp)" << endl;
                cout << "    ToR = " << pkt->get_crtToR() << ", port = " << pkt->get_crtport() <<
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
    updatePktIn(pkt.flow_id());
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

mem_b Queue::queuesize() {
    return _queuesize;
}

simtime_picosec Queue::serviceTime() {
    return _queuesize * _ps_per_byte;
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

void Queue::reportQueuesize() {
    cout << "Queue " << _tor << " " << _port << " " << queuesize() << endl;
}

void Queue::updatePktOut(uint64_t id){
    return;
    assert(_pkts_per_flow[id] > 0);
    _pkts_per_flow[id] -= 1; 
    if(_pkts_per_flow[id] == 0) {
        _flows_at_queues.erase(id);
    }
}

void Queue::updatePktIn(uint64_t id){
    return;
    _flows_at_queues.insert(id);
    _pkts_per_flow[id] += 1; 
    if(_pkts_per_flow[id] == 1){
        _flows_at_queues.insert(id);
    }
}

set<uint64_t> Queue::getFlows(){
    auto tmp = _flows_at_queues;
    _flows_at_queues.clear();
    return tmp;
}


//////////////////////////////////////////////////
//              Priority Queue                  //
//////////////////////////////////////////////////

PriorityQueue::PriorityQueue(linkspeed_bps bitrate, mem_b maxsize, 
			     EventList& eventlist, QueueLogger* logger, int node, DynExpTopology *top)
    : Queue(bitrate, maxsize, eventlist, logger)
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
    case SAMPLE:
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
    queue_priority_t prio = getPriority(pkt);

    // debug:
    //cout << "   NIC received a packet." << endl;

    //pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_ARRIVE);

    /* enqueue the packet */
    bool queueWasEmpty = false;
    if (queuesize() == 0)
        queueWasEmpty = true;

    _queuesize[prio] += pkt.size();
    _queue[prio].push_front(&pkt);

    //if (_logger)
    //    _logger->logQueue(*this, QueueLogger::PKT_ENQUEUE, pkt);

    updatePktIn(pkt.flow_id());
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
            eventlist().sourceIsPendingRel(*this, drainTime(_queue[prio].back()));
            //cout << "PrioQueue (node " << _node << ") sending a packet at " << timeAsUs(eventlist().now()) << " us" << endl;
            //cout << "   will be drained in " << timeAsUs(drainTime(_queue[prio].back())) << " us" << endl;

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

	        if (pkt->get_src_ToR() == _top->get_firstToR(pkt->get_dst())) {
	            // the packet is being sent within the same rack
	            pkt->set_lasthop(false);
	            pkt->set_crthop(-1);
	            pkt->set_crtToR(-1);
	            pkt->set_maxhops(0); // want to select a downlink port immediately
	        } else {
	            // the packet is being sent between racks

	            // we will choose the path based on the current slice
                int slice = _top->time_to_slice(eventlist().now());
	            // get the number of available paths for this packet during this slice
	            int npaths = _top->get_no_paths(pkt->get_src_ToR(),
	                _top->get_firstToR(pkt->get_dst()), slice);

	            if (npaths == 0)
	                cout << "Error: there were no paths for slice " << slice  << " src " << pkt->get_src_ToR() <<
                        " dst " << _top->get_firstToR(pkt->get_dst()) << endl;
	            assert(npaths > 0);

	            // randomly choose a path for the packet
	            // !!! todo: add other options like permutation, etc...
	            int path_index = random() % npaths;
                    //cout << "path_index " << path_index << endl;

	            pkt->set_slice_sent(slice); // "timestamp" the packet
                pkt->set_fabricts(eventlist().now());
                    pkt->set_queueing(0);
	            pkt->set_path_index(path_index); // set which path the packet will take

	            // set some initial packet parameters used for label switching
	            // *this could also be done in NDP before the packet is sent to the NIC
	            pkt->set_lasthop(false);
	            pkt->set_crthop(-1);
	            pkt->set_crtToR(-1);
	            pkt->set_maxhops(_top->get_no_hops(pkt->get_src_ToR(),
	                _top->get_firstToR(pkt->get_dst()), slice, path_index));
	        }

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
