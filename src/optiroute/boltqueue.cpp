// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "boltqueue.h"
#include <math.h>
#include "datacenter/dynexp_topology.h"
#include "ecn.h"
#include "eventlist.h"
#include "tcp.h"
#include "dctcp.h"
#include "queue_lossless.h"
#include "tcppacket.h"
#include "rlbmodule.h"
#include "rlbpacket.h"
#include <iostream>

extern uint32_t delay_host2ToR; // nanoseconds, host-to-tor link
extern uint32_t delay_ToR2ToR; // nanoseconds, tor-to-tor link

BoltQueue::BoltQueue(linkspeed_bps bitrate, mem_b maxsize, 
                     EventList& eventlist, QueueLogger* logger,
                     int tor, int port, DynExpTopology *top, Routing* routing)
: Queue(bitrate,maxsize,eventlist,logger,tor,port,top, routing)
{
  _CCthresh = 40*MTU_SIZE;
  _pru_token = 0;
  _sm_token = 0;
  int slices = top->get_nlogicslices();
  _servicing = Q_NONE;
#ifdef PRIO_QUEUE
  _enqueued[Q_LO].resize(slices);
  _queuesize[Q_LO].resize(slices);
  std::fill(_queuesize[Q_LO].begin(), _queuesize[Q_LO].end(), 0);
  _enqueued[Q_HI].resize(slices);
  _queuesize[Q_HI].resize(slices);
  std::fill(_queuesize[Q_HI].begin(), _queuesize[Q_HI].end(), 0);
#else
  _enqueued.resize(slices);
  _queuesize.resize(slices);
  std::fill(_queuesize.begin(), _queuesize.end(), 0);
#endif
  _queuesize_rlb = 0;
}

void
BoltQueue::updateSupply(Packet &pkt) {
  simtime_picosec interarrival_t = eventlist().now()-_last_sm_t;
  _last_sm_t = eventlist().now();
  uint64_t bw = 1E11/8; //bytes per second
  unsigned supply = bw * (interarrival_t * 1E-12);
  unsigned demand = pkt.size();
  assert(supply > demand);
  _sm_token += min((supply-demand), (unsigned)MTU_SIZE);
}

void
BoltQueue::receivePacket(Packet & pkt)
{
  int pktslice = pkt.get_crtslice();
#ifdef PRIO_QUEUE
  switch(pkt.type()) {
    case TCPACK:
      prio = Q_HI;
      break;
    default:
      prio = Q_LO; 
  }
  assert(prio == Q_HI || prio == Q_LO);
#endif
/*
  cout << nodename() << " receivePacket " << pkt.size() << " " << pkt.flow_id() << " " << queuesize() << " " << pkt.get_slice_sent() << " " << _top->time_to_slice(eventlist().now()) << 
    " " << eventlist().now() << endl;
    */

  if(pkt.type() == RLB) {
    //cout << "BoltQueue receivePacket Rlb " << _tor << " " << _port << " seqno " << ((RlbPacket*)&pkt)->seqno() << endl;
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

  //SRC packets only generated for data packets, one time per packet at most
  if(pkt.type() == TCP && !pkt.early_fb() && queuesize() > _CCthresh) {
    //cout << "BoltQueue " << _tor << " " << _port << " SRC triggered\n";
    pktINT pkt_int(queuesize(), _txbytes, eventlist().now());
    pkt.push_int("bolt", pkt_int);
    sendEarlyFeedback(&pkt);
    pkt.set_early_fb();
  //increase pru_tokens if packet is last in flow AND flow is not first BDP
  } else if(pkt.type() == TCP && ((TcpPacket*)&pkt)->last() && !((TcpPacket*)&pkt)->first()) {
    _pru_token++;
  } else if (pkt.type() == TCPACK && ((TcpAck*)&pkt)->bolt_inc()) {
    if (_pru_token > 0) {
      _pru_token--;
    } else if (_sm_token >= MTU_SIZE) {
      _sm_token -= MTU_SIZE;
    } else {
      ((TcpAck*)&pkt)->set_bolt_inc(false);
    }
  }

  /*
    if (queuesize() > _K && pkt.type() == TCP && !pkt.early_fb()){
        //TEST early fb in response to congestion
        sendEarlyFeedback(pkt);
        pkt.set_early_fb();
        //better to mark on dequeue, more accurate
        //pkt.set_flags(pkt.flags() | ECN_CE);
    }
*/

  /* enqueue the packet */
  bool queueWasEmpty = slice_queuesize(_crt_tx_slice) == 0;
#ifdef PRIO_QUEUE
  _enqueued[prio][pktslice].push_front(&pkt);
  _queuesize[prio][pktslice] += pkt.size();
  pkt.inc_queueing(_queuesize[prio][pktslice]);
  pkt.set_last_queueing(_queuesize[prio][pktslice]);
#else
  _enqueued[pktslice].push_front(&pkt);
  _queuesize[pktslice] += pkt.size();
#endif
  /*
    if(_top->is_last_hop(_port)) {
        cout << "CORE RATIO " << (double)_queuesize/pkt.get_queueing() <<  " " << _queuesize << " " << pkt.get_queueing() << endl;
    }
*/

  //record queuesize per_queuesize slice
  int slice = _top->time_to_slice(eventlist().now());
  if (queuesize() > _max_recorded_size_slice[slice]) {
    _max_recorded_size_slice[slice] = queuesize();
  }
  if (queuesize() > _max_recorded_size) {
    _max_recorded_size = queuesize();
  }

#ifdef PRIO_QUEUE
  if (queueWasEmpty && _queuesize[prio][_crt_tx_slice] > 0) {
    /* schedule the dequeue event */
    assert(_enqueued[prio][_crt_tx_slice].size() == 1);
#else
  if (queueWasEmpty && _queuesize[_crt_tx_slice] > 0) {
    /* schedule the dequeue event */
    assert(_enqueued[_crt_tx_slice].size() == 1);
#endif
    beginService();
  }
}

void BoltQueue::beginService() {
  if(_servicing == Q_RLB) {
    preemptRLB();
  }
  /* schedule the next dequeue event */
#ifdef PRIO_QUEUE
  assert(!_enqueued[Q_LO][_crt_tx_slice].empty() || !_enqueued[Q_HI][_crt_tx_slice].empty());
  if(!_enqueued[Q_HI][_crt_tx_slice].empty()) {
    assert(!_enqueued[Q_HI][_crt_tx_slice].empty());
    eventlist().sourceIsPendingRel(*this, drainTime(_enqueued[Q_HI][_crt_tx_slice].back()));
    _servicing = Q_HI;
  } else {
    assert(!_enqueued[Q_LO][_crt_tx_slice].empty());
    eventlist().sourceIsPendingRel(*this, drainTime(_enqueued[Q_LO][_crt_tx_slice].back()));
    _servicing = Q_LO;
  }
#else
  assert(!_enqueued[_crt_tx_slice].empty() || !_enqueued_rlb.empty());
  if(!_enqueued[_crt_tx_slice].empty()){
    simtime_picosec finish_time = eventlist().now()+drainTime(_enqueued[_crt_tx_slice].back());
    int finish_slice = _top->time_to_slice(finish_time);
    if(_top->is_reconfig(finish_time) && _port >= _top->no_of_hpr()) {
      cout << "hello world\n";
      finish_slice = _top->absolute_logic_slice_to_slice(finish_slice + 1);
    }
    if (finish_slice != _crt_tx_slice && !_top->is_downlink(_port)) {
      Packet *pkt = _enqueued[_crt_tx_slice].back();
      cout << "debug packet earlyfb? " << pkt->early_fb() << " TcpData? " << (pkt->type() == TCP) << " remaining " << _queuesize[_crt_tx_slice] << " slices " << finish_slice << " " << _crt_tx_slice << endl;
      //assert(0);
    } else {
      //cout << "beginService " << _tor << " " << _port << " id " << current_event_id << " slice " << _crt_tx_slice << endl;
      _servicing = Q_LO;
      eventlist().sourceIsPendingRel(*this, drainTime(_enqueued[_crt_tx_slice].back()));
    }
  } else if(!_enqueued_rlb.empty()) {
      //cout << "beginService RLB " << _tor << " " << _port << " slice " << _crt_tx_slice << endl;
      _servicing = Q_RLB;
      eventlist().sourceIsPendingRel(*this, drainTime(_enqueued_rlb.back()));
  } else {
    assert(0);
  }
#endif
}

void
BoltQueue::completeService()
{
  Packet *pkt = NULL;
  if(_servicing == Q_RLB) { 
    assert(!_enqueued_rlb.empty());
    pkt = _enqueued_rlb.back();

    DynExpTopology* top = pkt->get_topology();
    //cout << "BoltQueue Rlb completeService " << _tor << " " << _port << " seqno " << ((RlbPacket*)pkt)->seqno() << endl;

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
#ifdef PRIO_QUEUE
    assert(!_enqueued[_servicing][_crt_tx_slice].empty());
    Packet* pkt = _enqueued[_servicing][_crt_tx_slice].back();
    _enqueued[_servicing][_crt_tx_slice].pop_back();
    assert(_queuesize[_servicing][_crt_tx_slice] >= pkt->size());
    _queuesize[_servicing][_crt_tx_slice] -= pkt->size();
#else
    //cout << "completeService " << _tor << " " << _port << " id " << current_event_id << " slice " << _crt_tx_slice << endl;
    assert(!_enqueued[_crt_tx_slice].empty());
    pkt = _enqueued[_crt_tx_slice].back();
    _enqueued[_crt_tx_slice].pop_back();
    _queuesize[_crt_tx_slice] -= pkt->size();
#endif
  }

  if(pkt){
    sendFromQueue(pkt);
  }
  _servicing = Q_NONE;

#ifdef PRIO_QUEUE
  _servicing = Q_NONE;
  if (!_enqueued[Q_HI][_crt_tx_slice].empty() || !_enqueued[Q_LO][_crt_tx_slice].empty()) {
    /* schedule the next dequeue event */
    beginService();
  }
#else
  if (!_enqueued[_crt_tx_slice].empty() || !_enqueued_rlb.empty()) {
    /* schedule the next dequeue event */
    beginService();
  }
#endif
}

void 
BoltQueue::handleStuck() {
    Packet *pkt;
    list<Packet*> tmp = _enqueued[_crt_tx_slice];
    _enqueued[_crt_tx_slice].clear();
    _queuesize[_crt_tx_slice] = 0;
    while(!tmp.empty()) {
        pkt = tmp.back(); 
        tmp.pop_back();
        reroute(pkt);
    } 
}

void BoltQueue::reroute(Packet* pkt) {
  assert(pkt->get_crtslice() >= 0);
  simtime_picosec t = eventlist().now();
  simtime_picosec nxt_slice_time = _top->get_logic_slice_start_time(_top->time_to_absolute_logic_slice(t) + 1);
  pkt->set_src_ToR(pkt->get_crtToR());
  pkt->set_path_index(_routing->get_path_index(pkt, nxt_slice_time));
  pkt->set_slice_sent(_top->time_to_logic_slice(nxt_slice_time));
  pkt->set_hop_index(0);
  _routing->routing_from_ToR(pkt, nxt_slice_time, eventlist().now());
  Queue* nextqueue = _top->get_queue_tor(pkt->get_crtToR(), pkt->get_crtport());
  assert(nextqueue);
  nextqueue->receivePacket(*pkt);
}

void
BoltQueue::preemptRLB() {
  assert(_servicing == Q_RLB);
  eventlist().cancelPendingSource(*this);
  _servicing = Q_NONE;
}

mem_b
BoltQueue::queuesize() {
  int slices = _top->get_nlogicslices();
  mem_b size = 0;
  for(int i = 0; i < slices; i++){
#ifdef PRIO_QUEUE
    size += _queuesize[Q_LO][i] + _queuesize[Q_HI][i]; 
#else
    size += _queuesize[i]; 
#endif
  }
  return size;
}

mem_b BoltQueue::slice_queuesize(int slice){
    assert(slice <= _top->get_nlogicslices());
#ifdef PRIO_QUEUE
  return _queuesize[Q_LO][slice] + _queuesize[Q_HI][slice]; 
#else
  return _queuesize[slice];
#endif
}

simtime_picosec BoltQueue::get_queueing_delay(int slice){
    return slice_queuesize(slice)*_ps_per_byte;
}
