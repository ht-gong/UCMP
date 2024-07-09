// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "bolt.h"
#include "ecn.h"
#include "config.h"
#include "loggertypes.h"
#include "network.h"

string ntoa(double n);
extern unsigned total_flows;


////////////////////////////////////////////////////////////////
//  Bolt SOURCE
////////////////////////////////////////////////////////////////

BoltSrc::BoltSrc(TcpLogger* logger, TrafficLogger* pktlogger, EventList &eventlist, 
        DynExpTopology *top, int flow_src, int flow_dst, Routing *routing) 
    : TcpSrc(logger, pktlogger, eventlist, top, flow_src, flow_dst, routing)
{
    _past_cwnd = 2*Packet::data_packet_size();
    _rto = timeFromMs(10);    
    _use_tcp = false;
    _U = 1; //idk? will be used for EMWA later
    _last_sequpdate = 0;
    _last_seq_ai = 0;
    _is_new_slice = false;
}

void 
BoltSrc::startflow() {
    total_flows++;
    //_base_rtt = 0.000005; //seconds, average base rtt
    int num_hops = 1;
    if(_top->get_firstToR(_flow_src) != _top->get_firstToR(_flow_dst)){
        num_hops = _top->get_no_hops(_top->get_firstToR(_flow_src), _top->get_firstToR(_flow_dst), _top->time_to_slice(eventlist().now()), 0);
    }
    assert(num_hops <= 5);
    _base_rtt = 0.0000003 * num_hops * 2; //prop*hops*2 = base RTT
    _rtt = _base_rtt;
    _nic_rate = 1E11/8; //link bw bytes/seconds 
    //_bdp = _nic_rate*_base_rtt; //bdp
    _bdp = 1436*20; //bdp
    _cwnd = _bdp; //hpcc init cwnd = line rate
    //_crt_slice = _top->time_to_slice(eventlist().now());
}

void
BoltSrc::cleanup() {
    return;
}

//drop detected
void
BoltSrc::deflate_window(){
	  _ssthresh = max(_cwnd/2, (uint32_t)(2 * _mss));
    _past_cwnd = _cwnd;
}

void
BoltSrc::handleSRC(TcpAck *pkt) {
  double queuesize = pkt->get_int()["bolt"].qLen/1500.0; //queuesize in pkts
  simtime_picosec rtt_src = eventlist().now() - pkt->ts();
  uint64_t crt_rate = _cwnd / ((double)_rtt * 1E-12); //bytes/s
  //cout << "handleSRC cwnd " << _cwnd << " rtt " << _rtt << " crt_rate " << crt_rate << endl;
  double react_factor = min(crt_rate/_nic_rate, 1.0);
  double target_q = react_factor*queuesize; //in pkts
  if ((rtt_src/target_q) < (eventlist().now() - _last_dec_t)) {
    _cwnd -= _mss;
    _last_dec_t = eventlist().now();
  }
  //cout << "handleSRC crt_rate " << crt_rate <<  " queuesize " << queuesize << " target_q " << target_q << endl;
}

void
BoltSrc::handleAck(TcpAck *pkt) {
  if(pkt->bolt_inc()) {
    _cwnd += _mss;
    //cout << "handleAck INC cwnd" << _cwnd << endl;
  }
  if (pkt->ackno() >= _last_seq_ai) {
    _cwnd += _mss;
    _last_seq_ai = _highest_sent+1;
    //cout << "handleAck AI cwnd " << _cwnd << endl;
  }
}

void
BoltSrc::receivePacket(Packet& pkt) 
{
    assert(pkt.type() == TCPACK);
    TcpAck *p = (TcpAck*)(&pkt);
    int slice = p->get_tcp_slice();
    if(_finished) {
        cleanup();
        return TcpSrc::receivePacket(pkt);
    }
    
    //do not update cwnd using INT if slice changed
    int crt_slice = _top->time_to_slice(eventlist().now());
    if(pkt.early_fb()) {
      handleSRC(p); //SRC packet (sent back via early feedback)
    } else {
      handleAck(p); //ACK packet (can contain PRU/SM information)
    }

    if (_cwnd > _maxcwnd)
        _cwnd = _maxcwnd;

    _ssthresh = _cwnd;
    //cout << "Bolt receivePacket cwnd " << _cwnd << endl;
    TcpSrc::_cwnd = _cwnd;
    TcpSrc::receivePacket(pkt);
    //cout << ntoa(timeAsMs(eventlist().now())) << " ATCPID " << str() << " CWND " << _cwnd << " alfa " << ntoa(_alfa)<< endl;
}

void 
BoltSrc::rtx_timer_hook(simtime_picosec now,simtime_picosec period){
    TcpSrc::rtx_timer_hook(now,period);
};

void BoltSrc::doNextEvent() {
    if(!_rtx_timeout_pending) {
        startflow();
    }
    TcpSrc::doNextEvent();
}

