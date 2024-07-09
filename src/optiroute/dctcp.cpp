// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "dctcp.h"
#include "ecn.h"
//#include "mtcp.h"
#include "config.h"
#include "loggertypes.h"

string ntoa(double n);


////////////////////////////////////////////////////////////////
//  DCTCP SOURCE
////////////////////////////////////////////////////////////////

DCTCPSrc::DCTCPSrc(TcpLogger* logger, TrafficLogger* pktlogger, EventList &eventlist, 
        DynExpTopology *top, int flow_src, int flow_dst, Routing* routing)  : TcpSrc(logger, pktlogger, eventlist, top, flow_src, flow_dst, routing)
{
    _pkts_seen = 0;
    _pkts_marked = 0;
    _alfa = 0;
#ifdef TDTCP
    _past_cwnd.resize(_top->get_nslices());
    for(int i = 0; i < _past_cwnd.size(); i++)
        _past_cwnd[i] = 2*Packet::data_packet_size();
#else
    _past_cwnd = 2*Packet::data_packet_size();
#endif
    _rto = timeFromMs(10);    
}

//drop detected
#ifdef TDTCP
void
DCTCPSrc::deflate_window(int slice){
    _pkts_seen = 0;
    _pkts_marked = 0;
	_ssthresh[slice] = max(_cwnd[slice]/2, (uint32_t)(2 * _mss));

    _past_cwnd[slice] = _cwnd[slice];
}
#else
void
DCTCPSrc::deflate_window(){
    _pkts_seen = 0;
    _pkts_marked = 0;
	_ssthresh = max(_cwnd/2, (uint32_t)(2 * _mss));

    _past_cwnd = _cwnd;
}
#endif


void
DCTCPSrc::receivePacket(Packet& pkt) 
{
    TcpAck *p = (TcpAck*)(&pkt);
    int slice = p->get_tcp_slice();
    _pkts_seen++;

    if (pkt.flags() & ECN_ECHO){
    //cout << "marking\n";
    _pkts_marked += 1;

    //exit slow start since we're causing congestion
    if (_ssthresh>_cwnd)
      _ssthresh = _cwnd;
    }

#ifdef TDTCP
    if (_pkts_seen * _mss >= _past_cwnd[slice]){
#else
    if (_pkts_seen * _mss >= _past_cwnd){
#endif
	//update window, once per RTT
	
	double f = (double)_pkts_marked/_pkts_seen;
	//	cout << ntoa(timeAsMs(eventlist().now())) << " ID " << str() << " PKTS MARKED " << _pkts_marks;

	_alfa = 15.0/16.0 * _alfa + 1.0/16.0 * f;
	_pkts_seen = 0;
	_pkts_marked = 0;

#ifdef TDTCP
	if (_alfa>0){
	    _cwnd[slice] = _cwnd[slice] * (1-_alfa/2);

	    if (_cwnd[slice]<_mss)
		_cwnd[slice] = _mss;

	    _ssthresh = _cwnd;
	}
	_past_cwnd[slice] = _cwnd[slice];
#else
	if (_alfa>0){
	    _cwnd = _cwnd * (1-_alfa/2);

	    if (_cwnd<_mss)
		_cwnd = _mss;

	    _ssthresh = _cwnd;
	}
	_past_cwnd = _cwnd;
#endif

	//cout << ntoa(timeAsMs(eventlist().now())) << " UPDATE " << str() << " CWND " << _cwnd << " alfa " << ntoa(_alfa)<< " marked " << ntoa(f) << endl;
    }
  TcpSrc::receivePacket(pkt);
    //cout << ntoa(timeAsMs(eventlist().now())) << " ATCPID " << str() << " CWND " << _cwnd << " alfa " << ntoa(_alfa)<< endl;
}

void 
DCTCPSrc::rtx_timer_hook(simtime_picosec now,simtime_picosec period){
    TcpSrc::rtx_timer_hook(now,period);
};

