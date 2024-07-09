// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "ecnqueue.h"
#include <math.h>
#include "datacenter/dynexp_topology.h"
#include "ecn.h"
#include "tcp.h"
#include "dctcp.h"
#include "queue_lossless.h"
#include "tcppacket.h"
#include "rlbpacket.h"
#include "rlbmodule.h"
#include <iostream>
// #define DEBUG

ECNQueue::ECNQueue(linkspeed_bps bitrate, mem_b maxsize, 
			 EventList& eventlist, QueueLogger* logger, mem_b  K,
             int tor, int port, DynExpTopology *top, Routing* routing)
    : Queue(bitrate,maxsize,eventlist,logger,tor,port,top,routing), 
      _K(K)
{
    _state_send = LosslessQueue::READY;
    _top = top;
    // TODO: change
    int slices = top->get_nlogicslices();
    _dl_queue = slices;
    _enqueued.resize(slices + 1);
    _queuesize.resize(slices + 1);
    _queuesize_rlb = 0;
    std::fill(_queuesize.begin(), _queuesize.end(), 0);
    _servicing = Q_NONE;
}

void
ECNQueue::receivePacket(Packet & pkt)
{
  int pktslice = pkt.get_crtslice();
/*
  cout << nodename() << " receivePacket " << pkt.size() << " " << pkt.flow_id() << " " << queuesize() << " " << pkt.get_slice_sent() << " " << _top->time_to_slice(eventlist().now()) << 
    " " << eventlist().now() << endl;
    */

  if(pkt.type() == RLB) {
    //cout << "ECNQueue receivePacket Rlb " << _tor << " " << _port << " seqno " << ((RlbPacket*)&pkt)->seqno() << endl;
    _enqueued_rlb.push_back(&pkt);
    _queuesize_rlb += pkt.size();
    if(_servicing == Q_NONE) {
      beginService();
    }
    return;
  }

  if (queuesize()+pkt.size() > _maxsize) {
    /* if the packet doesn't fit in the queue, drop it */
    if(pkt.type() == TCP){
      TcpPacket *tcppkt = (TcpPacket*)&pkt;
      tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
    }
    cout << nodename() << " DROPPED " << queuesize() << " " << _top->time_to_slice(eventlist().now()) << endl;
    pkt.free();
    _num_drops++;
    return;
  }


  /* enqueue the packet */
  bool queueWasEmpty = slice_queuesize(_crt_tx_slice) == 0;
  _enqueued[pktslice].push_front(&pkt);
  _queuesize[pktslice] += pkt.size();

  //record queuesize per_queuesize slice
  int slice = _top->time_to_slice(eventlist().now());
  if (queuesize() > _max_recorded_size_slice[slice]) {
    _max_recorded_size_slice[slice] = queuesize();
  }
  if (queuesize() > _max_recorded_size) {
    _max_recorded_size = queuesize();
  }

  if (queueWasEmpty && _queuesize[_crt_tx_slice] > 0) {
    /* schedule the dequeue event */
    assert(_enqueued[_crt_tx_slice].size() == 1);
    beginService();
  }
}

#if 0
void
ECNQueue::receivePacket(Packet & pkt)
{
    if(pkt.id() == 60586) {
    cout << "receivePacket " << _tor << " " << _port << " " << pkt.get_crtslice() << " " << eventlist().now() << endl;
  }
    if(pkt.type() == RLB) {
        //bool queueWasEmpty = _enqueued[_crt_tx_slice].empty() && _enqueued_rlb.empty(); //is the current queue empty?
        _enqueued_rlb.push_front(&pkt);
        _queuesize_rlb += pkt.size();
        if (/*queueWasEmpty &&*/ !_sending_pkt) {
            /* schedule the dequeue event */
            if(!(_enqueued_rlb.size() == 1 && _enqueued[_crt_tx_slice].size() == 0)) cout << "rlbsize " << _enqueued_rlb.size() << " size " << _enqueued[_crt_tx_slice].size() << endl;
            assert(_enqueued_rlb.size() == 1 && _enqueued[_crt_tx_slice].size() == 0);
            beginService();
        } 
        return;
    }
    int pkt_slice = _top->is_downlink(_port) ? 0 : pkt.get_crtslice();

    if(pkt_slice != _crt_tx_slice){
        int slice_diff;
        if(pkt_slice > _crt_tx_slice) {
            slice_diff = pkt_slice - _crt_tx_slice;
        } else {
            slice_diff = pkt_slice - _crt_tx_slice + _top->get_nslices();
        }
        if(pkt.get_tcpsrc()->get_flowsize() < 20000 && slice_diff > 3) {
            cout << "BAD SLICE DIFF " << slice_diff << " flowsize " << pkt.get_tcpsrc()->get_flowsize() << endl;
        }
    }

    #ifdef DEBUG
    cout<< "Queue " << _tor << "," << _port << "Slice received from PKT:"<<pkt.get_crtslice()<<" at " <<eventlist().now()<< "delay: " << get_queueing_delay(pkt.get_crtslice()) << endl;
    dump_queuesize();
    #endif
    // dump_queuesize();
    if (pkt.size() == UINT16_MAX) {
         if(pkt.type() == TCP){
            TcpPacket *tcppkt = (TcpPacket*)&pkt;
            tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
            cout << "DROPPED because expander routing failed\n";
            dump_queuesize();
        }
        pkt.free();
        _num_drops++;
        return;
    }

    if (queuesize() + pkt.size() > _maxsize) {
        /* if the packet doesn't fit in the queue, drop it */
        /*
           if (_logger) 
           _logger->logQueue(*this, QueueLogger::PKT_DROP, pkt);
           pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_DROP);
           */
        if(pkt.type() == TCP){
            TcpPacket *tcppkt = (TcpPacket*)&pkt;
            tcppkt->get_tcpsrc()->add_to_dropped(tcppkt->seqno());
            //cout<<"Current slice:"<< _crt_tx_slice<<" Slice received from PKT:"<<pkt.get_crtslice()<<" at " <<eventlist().now()<< "\n";
            cout << "DROPPED because calendarqueue full\n";
            //dump_queuesize();
        }
        pkt.free();
        _num_drops++;
        return;
    }
    //pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_ARRIVE);

    bool queueWasEmpty = slice_queuesize(_crt_tx_slice) == 0; //is the current queue empty?
    //for early feedback, check for ECN at enqueue time
    if (queuesize() > _K){
      if(_early_fb_enabled && pkt.type() == TCP) {
        if(!pkt.early_fb()){
          sendEarlyFeedback(pkt);
        }
        pkt.set_early_fb();
        cout << "early fb " << _tor << " " << _port << endl;
      }
    }

    /* enqueue the packet */
    _enqueued[pkt_slice].push_front(&pkt);
    _queuesize[pkt_slice] += pkt.size();
    pkt.inc_queueing(queuesize());

    //record queuesize per slice
    int slice = _top->time_to_logic_slice(eventlist().now());
    if (queuesize() > _max_recorded_size_slice[slice]) {
        _max_recorded_size_slice[slice] = queuesize();
    }
    if (queuesize() > _max_recorded_size) {
        _max_recorded_size = queuesize();
    }
    
    if (queueWasEmpty && _queuesize[_crt_tx_slice] > 0) {
	/* schedule the dequeue event */
	    assert(_enqueued[_crt_tx_slice].size() == 1);
	    beginService();
    } 
    
}
#endif

void
ECNQueue::preemptRLB() {
  assert(_servicing == Q_RLB);
  eventlist().cancelPendingSource(*this);
  _servicing = Q_NONE;
}
/*
void ECNQueue::preemptRLB() {
    assert(_sending_pkt != NULL);
    assert(_sending_pkt->type() == RLB);
    _enqueued_rlb.push_front(_sending_pkt);
    eventlist().cancelPendingSource(*this);
    _sending_pkt = NULL;
    //_rlb_preempted = true;
}
*/

#if 0
void ECNQueue::beginService() {
    if(_sending_pkt != NULL && _sending_pkt->type() == RLB) {
        preemptRLB();
    }
    /* schedule the next dequeue event */
    assert(!_enqueued[_crt_tx_slice].empty() || !_enqueued_rlb.empty());
    //assert(drainTime(_enqueued[_crt_tx_slice].back()) != 0);
    Packet* to_be_sent = NULL;
    if(!_enqueued[_crt_tx_slice].empty()) {
        assert(_sending_pkt == NULL);
        /*
        if(_sending_pkt != NULL && _sending_pkt->type() == RLB) {
            preemptRLB();
        }
        */
        to_be_sent = _enqueued[_crt_tx_slice].back();
    } else if(!_enqueued_rlb.empty()) {
        assert(_sending_pkt == NULL);
        /*
        simtime_picosec finish_push = eventlist().now() + drainTime(_sending_pkt);
        int finish_push_slice = _top->time_to_logic_slice(finish_push); // plus the link delay
        if(_top->is_reconfig(finish_push)) {
            finish_push_slice = _top->absolute_logic_slice_to_slice(finish_push_slice + 1);
        }
        if(!_top->is_downlink(_port) && finish_push_slice != _crt_tx_slice) {
            // Uplink port attempting to serve pkt across configurations
            #ifdef DEBUG
            cout<<"RLB attempting to serve pkt across configurations\n";
            #endif
            _sending_pkt = NULL;
            return;
        }
        */
        _sending_pkt = _enqueued_rlb.back();
        _enqueued_rlb.pop_back();
        eventlist().sourceIsPendingRel(*this, drainTime(_sending_pkt));
        return;
    } else {
        assert(0);
    }
    if(to_be_sent->id() == 60586) {
    cout << "beginService " << _tor << " " << _port << " " << to_be_sent->get_crtslice() << " " << eventlist().now() << endl;
  }
    DynExpTopology* top = to_be_sent->get_topology();
    
    simtime_picosec finish_push = eventlist().now() + drainTime(to_be_sent) /*229760*/;

    int finish_push_slice = top->time_to_logic_slice(finish_push); // plus the link delay
    if(top->is_reconfig(finish_push)) {
        finish_push_slice = top->absolute_logic_slice_to_slice(finish_push_slice + 1);
    }

    if(!top->is_downlink(_port) && finish_push_slice != _crt_tx_slice) {
        // Uplink port attempting to serve pkt across configurations
        #ifdef DEBUG
        cout<<"Uplink port attempting to serve pkt across configurations\n";
        #endif
        Packet *pkt = _enqueued[_crt_tx_slice].back();
        cout << "debug packet earlyfb? " << pkt->early_fb() << " TcpData? " << (pkt->type() == TCP) << " remaining " << _queuesize[_crt_tx_slice] << " slices " << finish_push_slice << " " << _crt_tx_slice << " pktid " << pkt->id() << endl;
        // assert(0);
        cout<<"Uplink port attempting to serve pkt across configurations\n";
        _sending_pkt = NULL;
        if(_queuesize_rlb > 0) {
            _sending_pkt = _enqueued_rlb.back();
            _enqueued_rlb.pop_back();
            eventlist().sourceIsPendingRel(*this, drainTime(_sending_pkt));
        }
        return;
    }

    eventlist().sourceIsPendingRel(*this, drainTime(_enqueued[_crt_tx_slice].back()));
    _is_servicing = true;
    _last_service_begin = eventlist().now();
    _sending_pkt = _enqueued[_crt_tx_slice].back();
    _enqueued[_crt_tx_slice].pop_back();

    //mark on dequeue
    if (queuesize() > _K){
      _sending_pkt->set_flags(_sending_pkt->flags() | ECN_CE);
    }

    //_queuesize[_crt_tx_slice] -= _sending_pkt->size();
    
    #ifdef DEBUG
    unsigned seqno = 0;
    if(_sending_pkt->type() == TCP) {
        seqno = ((TcpPacket*)_sending_pkt)->seqno();
    } else if (_sending_pkt->type() == TCPACK) {
        seqno = ((TcpAck*)_sending_pkt)->ackno();
    }
    cout << "Queue " << _tor << "," << _port << " beginService seq " << seqno << 
        " pktslice" << _sending_pkt->get_crtslice() << " tx slice " << _crt_tx_slice << " at " << eventlist().now() <<
    " src,dst " << _sending_pkt->get_src() << "," << _sending_pkt->get_dst() << endl;
    dump_queuesize();
    #endif 
}
#endif

void ECNQueue::beginService() {
  if(_servicing == Q_RLB) {
    preemptRLB();
  }
  /* schedule the next dequeue event */
  assert(!_enqueued[_crt_tx_slice].empty() || !_enqueued_rlb.empty());
  if(!_enqueued[_crt_tx_slice].empty()){
    simtime_picosec finish_time = eventlist().now()+drainTime(_enqueued[_crt_tx_slice].back());
    int finish_slice = _top->time_to_slice(finish_time);
    if(_top->is_reconfig(finish_time) && _port >= _top->no_of_hpr()) {
      finish_slice = _top->absolute_logic_slice_to_slice(finish_slice + 1);
    }
    if (finish_slice != _crt_tx_slice && !_top->is_downlink(_port)) {
      Packet *pkt = _enqueued[_crt_tx_slice].back();
      cout << "debug packet earlyfb? " << pkt->early_fb() << " TcpData? " << (pkt->type() == TCP) << " remaining " << _queuesize[_crt_tx_slice] << " slices " << finish_slice << " " << _crt_tx_slice << endl;
      //assert(0);
    } else {
      //cout << "beginService " << _tor << " " << _port << " id " << current_event_id << " slice " << _crt_tx_slice << endl;
      _servicing = Q_LO;
      _is_servicing = true;
      _last_service_begin = eventlist().now();
      eventlist().sourceIsPendingRel(*this, drainTime(_enqueued[_crt_tx_slice].back()));
    }
  } else if(!_enqueued_rlb.empty()) {
      //cout << "beginService RLB " << _tor << " " << _port << " slice " << _crt_tx_slice << endl;
      _servicing = Q_RLB;
      eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_rlb.back()));
  } else {
    assert(0);
  }
}

void
ECNQueue::completeService()
{
  Packet *pkt = NULL;
  if(_servicing == Q_RLB) { 
    assert(!_enqueued_rlb.empty());
    pkt = _enqueued_rlb.back();

    DynExpTopology* top = pkt->get_topology();
    //cout << "ECNQueue Rlb completeService " << _tor << " " << _port << " seqno " << ((RlbPacket*)pkt)->seqno() << endl;

    bool pktfound = true;
    if (_port >= top->no_of_hpr()) { // it's a ToR uplink
      pktfound = false;
      while (!_enqueued_rlb.empty()) {

        pkt = _enqueued_rlb.back(); // get the next packet
        int slice = top->time_to_slice(eventlist().now());
        // get the destination ToR:
        int dstToR = top->get_firstToR(pkt->get_dst());
        // get the currently-connected ToR:
        int nextToR = top->get_nextToR(slice, pkt->get_crtToR(), pkt->get_crtport());

        if (dstToR == nextToR) {
          // this is a "fresh" RLB packet
          _enqueued_rlb.pop_back();
          _queuesize_rlb -= pkt->size();
          pktfound = true;
          break;
        } else {
          // this is an old packet, "drop" it and move on to the next one

          RlbPacket *p = (RlbPacket*)(pkt);

          // debug:
          //cout << "X dropped an RLB packet at port " << pkt->get_crtport() << ", seqno:" << p->seqno() << endl;
          //cout << "   ~ checked @ " << timeAsUs(eventlist().now()) << " us: specified ToR:" << dstToR << ", current ToR:" << nextToR << endl;

          // old version: just drop the packet
          //pkt->free();

          // new version: NACK the packet
          // NOTE: have not actually implemented NACK mechanism... Future work
          RlbModule* module = top->get_rlb_module(p->get_src()); // returns pointer to Rlb module that sent the packet
          module->receivePacket(*p, 1); // 1 means to put it at the front of the queue

          _enqueued_rlb.pop_back(); // pop the packet
          _queuesize_rlb -= pkt->size(); // decrement the queue size
        }
      }

      // we went through the whole queue and they were all "old" packets
      if (!pktfound){
        pkt = NULL;
      }

    } else { // its a ToR downlink
      _enqueued_rlb.pop_back();
      _queuesize_rlb -= pkt->size();
    }

  } else {
    /* dequeue the packet */
    //cout << "completeService " << _tor << " " << _port << " id " << current_event_id << " slice " << _crt_tx_slice << endl;
    assert(!_enqueued[_crt_tx_slice].empty());
    pkt = _enqueued[_crt_tx_slice].back();
    if (queuesize() > _K){
      pkt->set_flags(pkt->flags() | ECN_CE);
    }
    _enqueued[_crt_tx_slice].pop_back();
    _queuesize[_crt_tx_slice] -= pkt->size();
  }

  if(pkt){
    sendFromQueue(pkt);
  }
  _servicing = Q_NONE;
  _is_servicing = false;

  if (!_enqueued[_crt_tx_slice].empty() || !_enqueued_rlb.empty()) {
    /* schedule the next dequeue event */
    beginService();
  }
}

#if 0
void
ECNQueue::completeService()
{
    /* dequeue the packet */
    assert(_sending_pkt);
    if(_sending_pkt->id() == 60586) {
    cout << "completeService " << _tor << " " << _port << " " << _sending_pkt->get_crtslice() << " " << eventlist().now() << endl;
  }
    if(_sending_pkt->type() == RLB) {
		int ul = _top->no_of_hpr(); // !!! # uplinks = # downlinks = # hosts per rack

		if (_port >= ul) { // it's a ToR uplink

			// check if we're still connected to the right rack:
			
			int slice = _top->time_to_slice(eventlist().now());

            while (!_enqueued_rlb.empty() || _sending_pkt != NULL) {

                if(_sending_pkt == NULL){
            	    _sending_pkt = _enqueued_rlb.back(); // get the next packet
            		_enqueued_rlb.pop_back();
                }

            	// get the destination ToR:
            	int dstToR = _top->get_firstToR(_sending_pkt->get_dst());
            	// get the currently-connected ToR:
            	int nextToR = _top->get_nextToR(slice, _sending_pkt->get_crtToR(), _sending_pkt->get_crtport());
                //cout << "nxt " << nextToR << " dst " << dstToR << endl;

            	if (dstToR == nextToR && !_top->is_reconfig(eventlist().now())) {
            		// this is a "fresh" RLB packet
                    if(_queuesize_rlb < _sending_pkt->size()) {
                        cout << "ERR qsize " << _queuesize_rlb << " pktsize " << _sending_pkt->size() << " pkts# " << _enqueued_rlb.size() << endl;
                    }
            		break;
            	} else {
            		// this is an old packet, "drop" it and move on to the next one
            		RlbPacket *p = (RlbPacket*)(_sending_pkt);

            		// debug:
            		//cout << "X dropped an RLB packet at port " << pkt->get_crtport() << ", seqno:" << p->seqno() << endl;
            		//cout << "   ~ checked @ " << timeAsUs(eventlist().now()) << " us: specified ToR:" << dstToR << ", current ToR:" << nextToR << endl;
            		
            		// old version: just drop the packet
            		//pkt->free();

            		// new version: NACK the packet
            		// NOTE: have not actually implemented NACK mechanism... Future work
            		RlbModule* module = _top->get_rlb_module(p->get_src()); // returns pointer to Rlb module that sent the packet
    				module->receivePacket(*p, 1); // 1 means to put it at the front of the queue
                    _sending_pkt = NULL;
            	}
            }

		} else { // its a ToR downlink
		}
        if(_sending_pkt){
            assert(_queuesize_rlb >= _sending_pkt->size());
            _queuesize_rlb -= _sending_pkt->size();
            sendFromQueue(_sending_pkt);
        }
        _sending_pkt = NULL;
        _is_servicing = false; 

    if (!_enqueued[_crt_tx_slice].empty() || !_enqueued_rlb.empty()) {
        /* schedule the next dequeue event */
        beginService();
    }
    return;
	}
    if(_crt_tx_slice != _sending_pkt->get_crtslice()) {
    cout << "sending_pkt " << _sending_pkt->get_crtslice() << " efb " << _sending_pkt->early_fb() << " tx_slice " << _crt_tx_slice << " port " << _port << endl; 
    cout << "crt_tor " << _sending_pkt->get_crtToR() << " crt_port " << _sending_pkt->get_crtport() << " dst_tor " << _top->get_firstToR(_sending_pkt->get_dst()) << endl;
  }
    assert(_crt_tx_slice == _sending_pkt->get_crtslice());

    unsigned seqno = 0;
    if(_sending_pkt->type() == TCP) {
        seqno = ((TcpPacket*)_sending_pkt)->seqno();
    } else if (_sending_pkt->type() == TCPACK) {
        seqno = ((TcpAck*)_sending_pkt)->ackno();
    }
    
    #ifdef DEBUG
    cout << "Queue " << _tor << "," << _port << " completeService seq " << seqno << 
        " pktslice" << _sending_pkt->get_crtslice() << " tx slice " << _crt_tx_slice << " at " << eventlist().now() <<
    " src,dst " << _sending_pkt->get_src() << "," << _sending_pkt->get_dst() << endl;
    dump_queuesize();
    #endif
    
    _queuesize[_crt_tx_slice] -= _sending_pkt->size();

    if(_sending_pkt->id() == 2979155) {
      cout << "DEBUG completeService " << _tor << " " << _port << " crt_slice " << _crt_tx_slice << endl; 
  }
    if(_sending_pkt->id() == 2980467) {
      cout << "DEBUG EFB completeService " << _tor << " " << _port << " crt_slice " << _crt_tx_slice << " pktslice " << _sending_pkt->get_crtslice() << " hop " << _sending_pkt->get_hop_index() << endl;
  }
    sendFromQueue(_sending_pkt);
    _sending_pkt = NULL;
    _is_servicing = false;
    
    if (!_enqueued[_crt_tx_slice].empty() || !_enqueued_rlb.empty()) {
        /* schedule the next dequeue event */
        beginService();
    }
}
#endif

mem_b ECNQueue::slice_queuesize(int slice){
    assert(slice <= _dl_queue);
    //cout << _queuesize[slice] << " " << _queuesize_rlb << " " << _enqueued_rlb.size() << endl;
    //return _queuesize[slice]+_queuesize_rlb;
    return _queuesize[slice];
}

mem_b ECNQueue::queuesize() {
    if(_top->is_downlink(_port))
        return _queuesize[0];
    int sum = 0;
    for(int i = 0; i < _dl_queue; i++){
        sum += _queuesize[i];
    }
    return sum;
}

void ECNQueue::dump_queuesize() {
    cout<<nodename()<<":  ";
    for(int i = 0; i < _dl_queue; i++){
        cout<<i<<":"<<_queuesize[i]<<" ";
    }
    cout<<"\n";
}

simtime_picosec ECNQueue::get_queueing_delay(int slice) {
    return _queuesize[slice]*_ps_per_byte;
}

