// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "ecnqueue.h"
#include <math.h>
#include "datacenter/dynexp_topology.h"
#include "ecn.h"
#include "tcp.h"
#include "dctcp.h"
#include "queue_lossless.h"
#include "tcppacket.h"
#include <iostream>

ECNQueue::ECNQueue(linkspeed_bps bitrate, mem_b maxsize, 
			 EventList& eventlist, QueueLogger* logger, mem_b  K,
             int tor, int port, DynExpTopology *top)
    : Queue(bitrate,maxsize,eventlist,logger,tor,port,top), 
      _K(K)
{
    _state_send = LosslessQueue::READY;
}


void
ECNQueue::receivePacket(Packet & pkt)
{
    //is this a PAUSE packet?
    if (pkt.type()==ETH_PAUSE){
	EthPausePacket* p = (EthPausePacket*)&pkt;
	
	if (p->sleepTime()>0){
	    //remote end is telling us to shut up.
	    //assert(_state_send == LosslessQueue::READY);
	    if (queuesize()>0)
		//we have a packet in flight
		_state_send = LosslessQueue::PAUSE_RECEIVED;
	    else
		_state_send = LosslessQueue::PAUSED;
	    
	    //cout << timeAsMs(eventlist().now()) << " " << _name << " PAUSED "<<endl;
	}
	else {
	    //we are allowed to send!
	    _state_send = LosslessQueue::READY;
	    //cout << timeAsMs(eventlist().now()) << " " << _name << " GO "<<endl;
	    
	    //start transmission if we have packets to send!
	    if(queuesize()>0)
		beginService();
	}
	
	pkt.free();
	return;
    }


    if (_queuesize+pkt.size() > _maxsize) {
        /* if the packet doesn't fit in the queue, drop it */
        /*
           if (_logger) 
           _logger->logQueue(*this, QueueLogger::PKT_DROP, pkt);
           pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_DROP);
           */
        if(pkt.type() == TCP){
            TcpPacket *tcppkt = (TcpPacket*)&pkt;
            tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
            cout << "DROPPED\n";
        }
        pkt.free();
        _num_drops++;
        return;
    }
    //pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_ARRIVE);

    //mark on enqueue
    //    if (_queuesize > _K)
    //  pkt.set_flags(pkt.flags() | ECN_CE);

    /* enqueue the packet */
    updatePktIn(pkt.flow_id());
    bool queueWasEmpty = _enqueued.empty();
    _enqueued.push_front(&pkt);
    _queuesize += pkt.size();
    pkt.inc_queueing(_queuesize);
    pkt.set_last_queueing(_queuesize);
/*
    if(_top->is_last_hop(_port)) {
        cout << "CORE RATIO " << (double)_queuesize/pkt.get_queueing() <<  " " << _queuesize << " " << pkt.get_queueing() << endl;
    }
*/

    //record queuesize per slice
    int slice = _top->time_to_superslice(eventlist().now());
    if (queuesize() > _max_recorded_size_slice[slice]) {
        _max_recorded_size_slice[slice] = queuesize();
    }
    if (queuesize() > _max_recorded_size) {
        _max_recorded_size = queuesize();
    }

    if (queueWasEmpty && _state_send==LosslessQueue::READY) {
	/* schedule the dequeue event */
	assert(_enqueued.size() == 1);
	beginService();
    }
    
}

void
ECNQueue::completeService()
{
	/* dequeue the packet */
    assert(!_enqueued.empty());
    Packet* pkt = _enqueued.back();
    _enqueued.pop_back();

    if (_state_send==LosslessQueue::PAUSE_RECEIVED)
	_state_send = LosslessQueue::PAUSED;
    
    //mark on deque
    if (_queuesize > _K){
	  pkt->set_flags(pkt->flags() | ECN_CE);
          //cout << "ECNQueue " << _tor << " " << _port << " marked ECN size " << _queuesize << endl; 
    }

    _queuesize -= pkt->size();
    //pkt->flow().logTraffic(*pkt, *this, TrafficLogger::PKT_DEPART);
    //if (_logger) _logger->logQueue(*this, QueueLogger::PKT_SERVICE, *pkt);

    sendFromQueue(pkt);

    if (!_enqueued.empty() && _state_send==LosslessQueue::READY) {
	/* schedule the next dequeue event */
	beginService();
    }
}
