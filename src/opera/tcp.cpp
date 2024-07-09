// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#include "tcp.h"
//#include "mtcp.h"
#include "config.h"
#include "datacenter/dynexp_topology.h"
#include "ecn.h"
#include <iostream>
#include <algorithm>

#define KILL_THRESHOLD 5

static uint64_t id_gen;

//#define TCP_SACK
////////////////////////////////////////////////////////////////
//  TCP SOURCE
////////////////////////////////////////////////////////////////

TcpSrc::TcpSrc(TcpLogger* logger, TrafficLogger* pktlogger, 
	       EventList &eventlist, DynExpTopology *top, int flow_src, int flow_dst, bool longflow)
    : EventSource(eventlist,"tcp"),  _logger(logger), _flow(pktlogger),
      _top(top), _flow_src(flow_src), _flow_dst(flow_dst), _longflow(longflow)
{
    _mss = Packet::data_packet_size();
    //_maxcwnd = 0xffffffff;//MAX_SENT*_mss;
    _maxcwnd = 65535;//MAX_SENT*_mss;
    _sawtooth = 0;
    _subflow_id = -1;
    _rtt_avg = timeFromMs(0);
    _rtt_cum = timeFromMs(0);
    _base_rtt = timeInf;
    _cap = 0;
    _flow_size = ((uint64_t)1)<<63;
    _highest_sent = 0;
    _packets_sent = 0;
    _app_limited = -1;
    _established = false;
    _effcwnd = 0;

    //_ssthresh = 30000;
#ifdef TDTCP
    //damn this is heavy for memory. init only at flow start and free after finished
    /*
    _cwnd.resize(_top->get_nsuperslice());
    _ssthresh.resize(_top->get_nsuperslice());
    for(int i = 0; i < _ssthresh.size(); i++)
        _ssthresh[i] = 0xffffffff;
    */
#else
    //_ssthresh = 0xffffffff;
    _ssthresh = 65536;
#endif

    _last_acked = 0;
    _last_ping = timeInf;
    _dupacks = 0;
    _rtt = 0;
    _rto = timeFromMs(3000);
    _mdev = 0;
    _recoverq = 0;
    _in_fast_recovery = false;
    //_mSrc = NULL;
    _drops = 0;

    //_old_route = NULL;
    _last_packet_with_old_route = 0;

    _rtx_timeout_pending = false;
    _RFC2988_RTO_timeout = timeInf;
    _bulk_sender = new TcpBulkSender(*this, eventlist, top);

    _nodename = "tcpsrc";
}

/*
void TcpSrc::set_app_limit(int pktps) {
    if (_app_limited==0 && pktps){
	_cwnd = _mss;
    }
    _ssthresh = 0xffffffff;
    _app_limited = pktps;
    send_packets();
}
*/

void 
TcpSrc::startflow() {
    //cout << "startflow()\n";
#ifdef TDTCP
    _cwnd.resize(_top->get_nsuperslice());
    _ssthresh.resize(_top->get_nsuperslice());
    for(int i = 0; i < _ssthresh.size(); i++)
        _ssthresh[i] = 65536;
        //_ssthresh[i] = 0xffffffff;
    for(int i = 0; i < _cwnd.size(); i++)
        _cwnd[i] = 10*_mss;
    _unacked = _cwnd[0];
#else
    _cwnd = 10*_mss;
    _unacked = _cwnd;
#endif
    _established = false;
    
    //both long flows and short flows send SYN immediately
    send_packets();
}

void
TcpSrc::cleanup() {
#ifdef TDTCP
    _cwnd.clear();
    _ssthresh.clear();
#endif
    return;
}

void TcpSrc::add_to_dropped(uint64_t seqno) {
    _dropped_at_queue.push_back(seqno);
}

bool TcpSrc::was_it_dropped(uint64_t seqno, bool clear) {
    vector<uint64_t>::iterator it;
    it = find(_dropped_at_queue.begin(), _dropped_at_queue.end(), seqno);
    if (it != _dropped_at_queue.end()) {
        //cout << "DROPPED\n";
        if(clear) {
            _dropped_at_queue.erase(it);
        }
        return true;
    } else {
        return false;
    }
}
uint32_t TcpSrc::effective_window() {
#ifdef TDTCP
    int slice = _top->time_to_superslice(eventlist().now());
    return _in_fast_recovery? _ssthresh[slice] : _cwnd[slice];
#else
    return _in_fast_recovery? _ssthresh : _cwnd;
#endif
}

void TcpSrc::reportTP() {
    simtime_picosec sample_duration = eventlist().now()-_last_sample; 
    float tp = _bytes_in_sample/(long double)(sample_duration/1E12);
    cout << "TP " << _flow_id << " " << tp << endl;
    _last_sample = eventlist().now();
    _bytes_in_sample = 0;
}

void TcpSrc::cmpIdealCwnd(uint64_t ideal_mbps){
    long double rtt = 0.00000524288;
    int slice = _top->time_to_superslice(eventlist().now());
    uint32_t ideal_cwnd = ((double)ideal_mbps*1E6/8)*rtt;
#ifdef TDTCP
    cout << "CWND " << _flow_id << " " << get_flow_src() << " " << get_flow_dst() <<
        " " << slice << " " << _flow_size << " " << eventlist().now() <<
	" ideal mbps " << (double)ideal_mbps/8 << " rtt " << (long double)_rtt/1E12 <<
        " " << " IDEAL " << ideal_cwnd << " ACTUAL " << _cwnd[slice] << endl;
#else
    cout << "CWND " << _flow_id << " " << get_flow_src() << " " << get_flow_dst() << 
        " " << slice << " " << _flow_size << " " << eventlist().now() <<
	" ideal mbps " << (double)ideal_mbps/8 << " rtt " << (long double)_rtt/1E12 <<
        " " << " IDEAL " << ideal_cwnd << " ACTUAL " << _cwnd << endl;
#endif
}

void 
TcpSrc::connect(TcpSink& sink, 
		simtime_picosec starttime) {
    //_route = &routeout;

    //assert(_route);
    _sink = &sink;
    _flow.id = id; // identify the packet flow with the TCP source that generated it
    _sink->connect(*this);
    _start_time = starttime;
    //cout << "Flow start " << _flow_src << " " << _flow_dst << " " << starttime << endl;

    //printf("Tcp %x msrc %x\n",this,_mSrc);
    eventlist().sourceIsPending(*this,starttime);
}

Queue* 
TcpSrc::sendToNIC(Packet* pkt) {
    DynExpTopology* top = pkt->get_topology();
    Queue* nic = top->get_queue_serv_tor(pkt->get_src()); // returns pointer to nic queue
    assert(nic);
    nic->receivePacket(*pkt); // send this packet to the nic queue
    return nic; // return pointer so NDP source can update send time
}

#define ABS(X) ((X)>0?(X):-(X))

void
TcpSrc::receivePacket(Packet& pkt) 
{
    simtime_picosec ts;
    TcpAck *p = (TcpAck*)(&pkt);
    TcpAck::seq_t seqno = p->ackno();
    list<pair<uint64_t, uint64_t>> sacks = p->get_sack();
    bool is_sack_loss;
    //we update slice for TDTCP based on corresponding data packet slice
    int pktslice = p->get_tcp_slice();
    int slice = _top->time_to_superslice(eventlist().now());
    //cout << "TcpSrc receive packet ackno:" << seqno << endl;

    //pkt.flow().logTraffic(pkt,*this,TrafficLogger::PKT_RCVDESTROY);

    ts = p->ts();
    packetid_t pktid = p->id();
    vector<int> rtt_path = p->get_path();
    p->clear_path();
    p->free();

    if (_finished || seqno < _last_acked) {
        //cout << "O seqno" << seqno << " last acked "<< _last_acked;
        if(_finished) _sink->_used_paths.clear();
        return;
    }

    if (seqno==1){
        //assert(!_established);
        _established = true;
    }
    else if (seqno>1 && !_established) {
        cout << "Should be _established " << seqno << endl;
    }

    //assert(seqno >= _last_acked);  // no dups or reordering allowed in this simple simulator

    //compute rtt
    uint64_t m = eventlist().now()-ts;
    /*
    if(random()%20 == 0){
    cout << "RTTPATH " << m << " ";
    for(int hop : rtt_path) {
        cout << hop << " ";
    }
    cout << endl;
    }
    */

    if (m!=0){
        if (_rtt>0){
            uint64_t abs;
            if (m>_rtt)
                abs = m - _rtt;
            else
                abs = _rtt - m;

            _mdev = 3 * _mdev / 4 + abs/4;
            _rtt = 7*_rtt/8 + m/8;
            _rto = _rtt + 4*_mdev;
        } else {
            _rtt = m;
            _mdev = m/2;
            _rto = _rtt + 4*_mdev;
        }
        if (_base_rtt==timeInf || _base_rtt > m)
            _base_rtt = m;
    }
    //  cout << "Base "<<timeAsMs(_base_rtt)<< " RTT " << timeAsMs(_rtt)<< " Queued " << queued_packets << endl;

    if (_rto<timeFromMs(1))
        _rto = timeFromMs(1);

    if (seqno >= _flow_size && !_finished){
        cout << "FCT " << get_flow_src() << " " << get_flow_dst() << " " << get_flowsize() <<
            " " << timeAsMs(eventlist().now() - get_start_time()) << " " << fixed 
            << timeAsMs(get_start_time()) << " " << _found_reorder << " " << _found_retransmit << " " << buffer_change << endl;
	/*
        cout << "PATHS " << get_flow_src() << " " << get_flow_dst() << " " << get_flowsize() << " ";
        for(vector<int> path : _sink->_used_paths) {
            bool first = true;
            for(int p : path) {
                if(!first) cout << ",";
                cout << p;
                first = false;
            }
            cout << " ";
        }
        cout << endl;
        */
        _sink->_used_paths.clear();
        //if (_found_reorder == 0) assert(_found_retransmit == 0);
        /*
        if (get_flow_src() == 355 && get_flow_dst() == 429) {
            exit(1);
        }
        */
        _finished = true;
        cleanup();
        return;
    }

#ifdef TCP_SACK
    _sack_handler.update(_last_acked, sacks, ts);
#endif
    if (seqno > _last_acked) { // a brand new ack
        _RFC2988_RTO_timeout = eventlist().now() + _rto;// RFC 2988 5.3
        _last_ping = eventlist().now();
        _bytes_in_sample += seqno-_last_acked;

        if (seqno >= _highest_sent) {
            _highest_sent = seqno;
            _RFC2988_RTO_timeout = timeInf;// RFC 2988 5.2
            _last_ping = timeInf;
        }

#ifdef TCP_SACK
        is_sack_loss = _sack_handler.isLost(_last_acked+1);
        if (!_in_fast_recovery && !is_sack_loss) { //can also infer loss from sack, same behavior as 3dupack
#else
        if (!_in_fast_recovery) { // best behaviour: proper ack of a new packet, when we were expecting it
                                  //clear timers
#endif

            _last_acked = seqno;
            _dupacks = 0;

#ifdef TDTCP
            inflate_window(pktslice);
#else
            inflate_window();
#endif

#ifdef TDTCP
            if (_cwnd[slice]>_maxcwnd) {
                _cwnd[slice] = _maxcwnd;
            }

            _unacked = _cwnd[slice];
            _effcwnd = _cwnd[slice];
#else
            if (_cwnd>_maxcwnd) {
                _cwnd = _maxcwnd;
            }

            _unacked = _cwnd;
            _effcwnd = _cwnd;
#endif
            if(_longflow){
                _bulk_sender->sendOrSchedule();
            } else {
                send_packets();
            }
            return;
        }
        // We're in fast recovery, i.e. one packet has been
        // dropped but we're pretending it's not serious
        if (seqno >= _recoverq) { 
            // got ACKs for all the "recovery window": resume
            // normal service
            uint32_t flightsize = _highest_sent - seqno;
	    //FD reordering euristics?
	    /*
	    if(eventlist().now() - _fast_recovery_start <= _rtt) {
            cout << "EURISTICS ts " << eventlist().now() << " frt " << _fast_recovery_start << " rtt " << _rtt << endl;
            _ssthresh = _old_ssthresh;
	    assert(_old_ssthresh != 0);
	    } else {
            cout << "NOEURISTICS ts " << eventlist().now() << " frt " << _fast_recovery_start << " rtt " << _rtt << endl;
            _cwnd = min(_ssthresh, flightsize + _mss);
	    }
	    */
#ifdef TDTCP
            _cwnd[pktslice] = min(_ssthresh[pktslice], flightsize + _mss);
            _unacked = _cwnd[pktslice];
            _effcwnd = _cwnd[pktslice];
#else
            _cwnd = min(_ssthresh, flightsize + _mss);
            _unacked = _cwnd;
            _effcwnd = _cwnd;
#endif
            _last_acked = seqno;
            _dupacks = 0;
            _in_fast_recovery = false;

            if(_longflow){
                _bulk_sender->sendOrSchedule();
            } else {
                send_packets();
            }
            return;
        }
        // In fast recovery, and still getting ACKs for the
        // "recovery window"
        // This is dangerous. It means that several packets
        // got lost, not just the one that triggered FR.
        uint32_t new_data = seqno - _last_acked;
        _last_acked = seqno;
#ifdef TDTCP
        //track sliced network that lost the packet for TDTCP
        _fast_recovery_slice = pktslice;
        _rtx_to_slice[_last_acked+1] = pktslice;
        if (new_data < _cwnd[pktslice]) 
            _cwnd[pktslice] -= new_data; 
        else 
            _cwnd[pktslice] = 0;
        _cwnd[pktslice] += _mss;
#else
        if (new_data < _cwnd) 
            _cwnd -= new_data; 
        else 
            _cwnd = 0;
        _cwnd += _mss;
#endif
        retransmit_packet();
        if(_longflow){
            _bulk_sender->sendOrSchedule();
        } else {
            send_packets();
        }
        return;
    }
    // It's a dup ack
    if (_in_fast_recovery) { // still in fast recovery; hopefully the prodigal ACK is on its way 
#ifdef TDTCP
        _cwnd[pktslice] += _mss;
        if (_cwnd[pktslice] > _maxcwnd) {
            _cwnd[pktslice] = _maxcwnd;
        }
        // When we restart, the window will be set to
        // min(_ssthresh, flightsize+_mss), so keep track of
        // this
        _unacked = min(_ssthresh[pktslice], (uint32_t)(_highest_sent-_recoverq+_mss)); 
        if (_last_acked+_cwnd[pktslice] >= _highest_sent+_mss) 
            _effcwnd=_unacked; // starting to send packets again
#else
        _cwnd += _mss;
        if (_cwnd>_maxcwnd) {
            _cwnd = _maxcwnd;
        }
        // When we restart, the window will be set to
        // min(_ssthresh, flightsize+_mss), so keep track of
        // this
        _unacked = min(_ssthresh, (uint32_t)(_highest_sent-_recoverq+_mss)); 
        if (_last_acked+_cwnd >= _highest_sent+_mss) 
            _effcwnd=_unacked; // starting to send packets again
#endif
        if(_longflow){
            _bulk_sender->sendOrSchedule();
        } else {
            send_packets();
        }
        return;
    }
    // Not yet in fast recovery. What should we do instead?
    _dupacks++;

#ifdef TCP_SACK
        if (_dupacks!=3 && !is_sack_loss)
#else
        if (_dupacks!=3) 
#endif
        { // not yet serious worry
            /*
            if (_logger) 
                _logger->logTcp(*this, TcpLogger::TCP_RCV_DUP);
            */
            if(_longflow){
                _bulk_sender->sendOrSchedule();
            } else {
                send_packets();
            }
            return;
        }
    // _dupacks==3
    if (_last_acked < _recoverq) {  
        /* See RFC 3782: if we haven't recovered from timeouts
           etc. don't do fast recovery */
        /*
        if (_logger) 
            _logger->logTcp(*this, TcpLogger::TCP_RCV_3DUPNOFR);
        */
        return;
    }

    // begin fast recovery
    
    //only count drops in CA state
    _drops++;
#ifdef TDTCP
    //track sliced network that lost the packet for TDTCP
    _fast_recovery_slice = pktslice;
    _rtx_to_slice[_last_acked+1] = pktslice;
#endif
    //print if retransmission is due to reordered packet (was not dropped)
    //also as we're retransmitting it, clear the seqno from the dropped list
    if (!was_it_dropped(_last_acked+1, true)) {
/*
        cout << "RETRANSMIT " << _flow_src << " " << _flow_dst << " " << _flow_size  << " " << seqno << endl;
        _found_retransmit++;
*/
#define ORACLE
#ifdef ORACLE
        //we know this was not dropped, do not deflate window/retransmit
        return;
#endif
    }

#ifdef TDTCP
    _old_ssthresh = _ssthresh[pktslice];
    deflate_window(pktslice);
#else
    _old_ssthresh = _ssthresh;
    deflate_window();
#endif

    if (_sawtooth>0)
        _rtt_avg = _rtt_cum/_sawtooth;
    else
        _rtt_avg = timeFromMs(0);

    _sawtooth = 0;
    _rtt_cum = timeFromMs(0);

    retransmit_packet();
#ifdef TDTCP
    _cwnd[pktslice] = _ssthresh[pktslice] + 3 * _mss;
    _unacked = _ssthresh[pktslice];
#else
    _cwnd = _ssthresh + 3 * _mss;
    _unacked = _ssthresh;
#endif
    _effcwnd = 0;
    _in_fast_recovery = true;
    _fast_recovery_start = eventlist().now();
    _recoverq = _highest_sent; // _recoverq is the value of the
                               // first ACK that tells us things
                               // are back on track
}

#ifdef TDTCP
void TcpSrc::deflate_window(int slice){
	assert(_ssthresh[slice] != 0);
	_old_ssthresh = _ssthresh[slice];
	_ssthresh[slice] = max(_cwnd[slice]/2, (uint32_t)(2 * _mss));
}

void
TcpSrc::inflate_window(int slice) {
    int newly_acked = (_last_acked + _cwnd[slice]) - _highest_sent;
    // be very conservative - possibly not the best we can do, but
    // the alternative has bad side effects.
    if (newly_acked > _mss) newly_acked = _mss; 
    if (newly_acked < 0)
        return;
    if (_cwnd < _ssthresh) { //slow start
        int increase = min(_ssthresh[slice] - _cwnd[slice], (uint32_t)newly_acked);
        _cwnd[slice] += increase;
        newly_acked -= increase;
    } else {
        // additive increase
        uint32_t pkts = _cwnd[slice]/_mss;

        double queued_fraction = 1 - ((double)_base_rtt/_rtt);

        if (queued_fraction>=0.5&&_cap)
            return;

        _cwnd[slice] += (newly_acked * _mss) / _cwnd[slice];  //XXX beware large windows, when this increase gets to be very small

        if (pkts!=_cwnd[slice]/_mss) {
            _rtt_cum += _rtt;
            _sawtooth ++;
        }
    }
    if (_cwnd[slice] > _maxcwnd) _cwnd[slice] = _maxcwnd;
}
#else
void TcpSrc::deflate_window(){
	assert(_ssthresh != 0);
	_old_ssthresh = _ssthresh;
	_ssthresh = max(_cwnd/2, (uint32_t)(2 * _mss));
}

void
TcpSrc::inflate_window() {
    int newly_acked = (_last_acked + _cwnd) - _highest_sent;
    // be very conservative - possibly not the best we can do, but
    // the alternative has bad side effects.
    if (newly_acked > _mss) newly_acked = _mss; 
    if (newly_acked < 0)
        return;
    if (_cwnd < _ssthresh) { //slow start
        int increase = min(_ssthresh - _cwnd, (uint32_t)newly_acked);
        _cwnd += increase;
        newly_acked -= increase;
    } else {
        // additive increase
        uint32_t pkts = _cwnd/_mss;

        double queued_fraction = 1 - ((double)_base_rtt/_rtt);

        if (queued_fraction>=0.5&&_cap)
            return;

        _cwnd += (newly_acked * _mss) / _cwnd;  //XXX beware large windows, when this increase gets to be very small

        if (pkts!=_cwnd/_mss) {
            _rtt_cum += _rtt;
            _sawtooth ++;
        }
    }
    if (_cwnd > _maxcwnd) _cwnd = _maxcwnd;
}
#endif


// Note: the data sequence number is the number of Byte1 of the packet, not the last byte.
void 
TcpSrc::send_packets() {
    int slice = _top->time_to_superslice(eventlist().now());
#ifdef TDTCP
    int c = _cwnd[slice];
#else
    int c = _cwnd;
#endif

    if (!_established){
        //cout << "need to establish\n";
        //send SYN packet and wait for SYN/ACK
        TcpPacket * p  = TcpPacket::new_syn_pkt(_top, _flow, _flow_src, _flow_dst, this, _sink, 1, 1);
        p->set_flow_id(_flow_id);
        assert(p->size() == 1+HEADER_SIZE);
        _highest_sent = 1;

        sendToNIC(p);

        if(_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
            _RFC2988_RTO_timeout = eventlist().now() + _rto;
        }	
        //cout << "Sending SYN, waiting for SYN/ACK" << endl;
        return;
    }

    if (_app_limited >= 0 && _rtt > 0) {
        uint64_t d = (uint64_t)_app_limited * _rtt/1000000000;
        if (c > d) {
            c = d;
        }
        //if (c<1000)
        //c = 1000;

        if (c==0){
            //      _RFC2988_RTO_timeout = timeInf;
        }

        //rtt in ms
        //printf("%d\n",c);
    }
#ifdef TCP_SACK
    //sack system will try to send possibly lost packets before sending new data
    if(_in_fast_recovery) {
        uint64_t in_flight = _sack_handler.setPipe();
        while(_cwnd - in_flight >= _mss) {
            uint64_t nextseq = _sack_handler.nextSeg();
            TcpPacket* p = TcpPacket::newpkt(_top, _flow, _flow_src, _flow_dst, this, _sink, 
                    nextseq, 0, _mss);
            p->set_flow_id(_flow_id);
            p->set_ts(eventlist().now());
            if(nextseq <= _highest_sent) {
                _sack_handler.updateHiRtx(nextseq);
            }
            if(nextseq > _highest_sent) {
                _sack_handler.updateHiData(nextseq);
            }

            sendToNIC(p);
            if(_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
                _RFC2988_RTO_timeout = eventlist().now() + _rto;
            }
            in_flight += _mss;
        }
    } else {
#endif

    while ((_last_acked + c >= _highest_sent + _mss) 
            && (_highest_sent < _flow_size)) {
        uint64_t data_seq = 0;

        uint16_t size = _highest_sent+_mss <= _flow_size ? _mss : _flow_size-_highest_sent+1;
        TcpPacket* p = TcpPacket::newpkt(_top, _flow, _flow_src, _flow_dst, this, _sink, _highest_sent+1, data_seq, size);
        //cout << "sending seqno:" << p->seqno() << endl;
	p->set_packetid(id_gen++);
        p->set_flow_id(_flow_id);
        p->set_ts(eventlist().now());
        p->set_tcp_slice(slice);

        _highest_sent += size;  //XX beware wrapping
        _packets_sent += size;
#ifdef TCP_SACK
        _sack_handler.updateHiData(_highest_sent);
#endif

        sendToNIC(p);

        if(_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
            _RFC2988_RTO_timeout = eventlist().now() + _rto;
        }
    }
#ifdef TCP_SACK
    }
#endif
    //cout << "stopped sending last_acked " << _last_acked << " _highest_sent " << _highest_sent << " c " << c << endl;
}

void 
TcpSrc::retransmit_packet() {
    if (!_established){
        assert(_highest_sent == 1);

        TcpPacket* p  = TcpPacket::new_syn_pkt(_top, _flow, _flow_src, _flow_dst, this, _sink, 1, 1);
        p->set_flow_id(_flow_id);
        sendToNIC(p);

        cout << "Resending SYN, waiting for SYN/ACK" << endl;
        return;	
    }

    uint64_t data_seq = 0;

    TcpPacket* p = TcpPacket::newpkt(_top, _flow, _flow_src, _flow_dst, this, _sink, _last_acked+1, data_seq, _pkt_size);
#ifdef TCP_SACK
    _sack_handler.updateHiRtx(_last_acked+1);
#endif

    //p->flow().logTraffic(*p,*this,TrafficLogger::PKT_CREATESEND);
    p->set_flow_id(_flow_id);
    p->set_ts(eventlist().now());
    sendToNIC(p);

    _packets_sent += _mss;

    if(_RFC2988_RTO_timeout == timeInf) {// RFC2988 5.1
        _RFC2988_RTO_timeout = eventlist().now() + _rto;
    }
}

void TcpSrc::rtx_timer_hook(simtime_picosec now, simtime_picosec period) {
    if (now <= _RFC2988_RTO_timeout || _RFC2988_RTO_timeout==timeInf || _finished) 
        return;

    if (_highest_sent == 0) 
        return;
    #ifdef TDTCP
    int slice = _top->time_to_superslice(eventlist().now());
    cout <<"At " << now/(double)1000000000<< " RTO " << _rto/1000000000 << " MDEV " 
        << _mdev/1000000000 << " RTT "<< _rtt/1000000000 << " SEQ " << _last_acked / _mss << " HSENT "  << _highest_sent 
        << " CWND "<< _cwnd[slice]/_mss << " FAST RECOVERY? " << 	_in_fast_recovery << " Flow ID " 
        << str()  << endl;
#else
    cout <<"At " << now/(double)1000000000<< " RTO " << _rto/1000000000 << " MDEV " 
        << _mdev/1000000000 << " RTT "<< _rtt/1000000000 << " SEQ " << _last_acked / _mss << " HSENT "  << _highest_sent 
        << " CWND "<< _cwnd/_mss << " FAST RECOVERY? " << 	_in_fast_recovery << " Flow ID " 
        << str()  << endl;
#endif

    // here we can run into phase effects because the timer is checked
    // only periodically for ALL flows but if we keep the difference
    // between scanning time and real timeout time when restarting the
    // flows we should minimize them !
    if(!_rtx_timeout_pending) {
        _rtx_timeout_pending = true;

        // check the timer difference between the event and the real value
        simtime_picosec too_late = now - (_RFC2988_RTO_timeout);

        // careful: we might calculate a negative value if _rto suddenly drops very much
        // to prevent overflow but keep randomness we just divide until we are within the limit
        while(too_late > period) too_late >>= 1;

        // carry over the difference for restarting
        simtime_picosec rtx_off = (period - too_late)/200;

        eventlist().sourceIsPendingRel(*this, rtx_off);

        //reset our rtx timerRFC 2988 5.5 & 5.6

        _rto *= 2;
        //if (_rto > timeFromMs(1000))
        //  _rto = timeFromMs(1000);
        _RFC2988_RTO_timeout = now + _rto;
    }
}

void TcpSrc::doNextEvent() {
    //TODO how to manage timeouts in TDTCP exactly?
    if(_rtx_timeout_pending) {
    int slice = _top->time_to_superslice(eventlist().now());
	_rtx_timeout_pending = false;

	if (_in_fast_recovery) {
#ifdef TDTCP
	    uint32_t flightsize = _highest_sent - _last_acked;
	    _cwnd[slice] = min(_ssthresh[slice], flightsize + _mss);
	}

	deflate_window(slice);

	_cwnd[slice] = _mss;

	_unacked = _cwnd[slice];
	_effcwnd = _cwnd[slice];
#else
	    uint32_t flightsize = _highest_sent - _last_acked;
	    _cwnd = min(_ssthresh, flightsize + _mss);
	}

	deflate_window();

	_cwnd = _mss;

	_unacked = _cwnd;
	_effcwnd = _cwnd;

#endif
	_in_fast_recovery = false;
	_recoverq = _highest_sent;

	if (_established)
	    _highest_sent = _last_acked + _mss;

	_dupacks = 0;

	retransmit_packet();

	if (_sawtooth>0)
	    _rtt_avg = _rtt_cum/_sawtooth;
	else
	    _rtt_avg = timeFromMs(0);

	_sawtooth = 0;
	_rtt_cum = timeFromMs(0);
    } else {
	startflow();
    }
}

////////////////////////////////////////////////////////////////
//  TCP SINK
////////////////////////////////////////////////////////////////

TcpSink::TcpSink() 
    : Logged("sink"), _cumulative_ack(0) , _packets(0), _crt_path(0)
{
    _nodename = "tcpsink";
}

void 
TcpSink::connect(TcpSrc& src) {
    _src = &src;
    _cumulative_ack = 0;
    _drops = 0;
}

// Note: _cumulative_ack is the last byte we've ACKed.
// seqno is the first byte of the new packet.
void
TcpSink::receivePacket(Packet& pkt) {
    //cout << "TcpSink receivePacket" << endl;
    TcpPacket *p = (TcpPacket*)(&pkt);
    TcpPacket::seq_t seqno = p->seqno();
    simtime_picosec ts = p->ts();
    simtime_picosec fts = p->get_fabricts();
    int pktslice = p->get_tcp_slice();

    //randomly sample packets for queueing
    if(random()%100 == 0){
        cout << "PKT " << p->get_queueing() << " " << p->get_last_queueing() << " " << _src->eventlist().now()-fts << endl;
    }

    //check paths
    vector<int> used_path = pkt.get_path();
    //new path, push back
    if(find(_used_paths.begin(), _used_paths.end(), used_path) == _used_paths.end()) {
        _used_paths.push_back(used_path);
    }
    p->clear_path();
    /*
    if(_src->get_flow_src() == 506 && _src->get_flow_dst() == 337) {
    cout << "PKT " << fts << " " << seqno << "AT " << _src->eventlist().now() << endl;
    }
    */

    bool marked = p->flags()&ECN_CE;
    
    int size = p->size()-HEADER_SIZE;
    assert(size > 0);
    //pkt.flow().logTraffic(pkt,*this,TrafficLogger::PKT_RCVDESTROY);
    if (last_ts > fts){
/*
        cout << "REORDER " << " " << _src->get_flow_src()<< " " << _src->get_flow_dst() << " "
            << _src->get_flowsize() << " " << 
            "EARLY " << last_ts << " " << last_hops << " " << last_queueing << " " << last_seqno << " " 
            "LATE " << fts << " " << p->get_crthop() << " " << p->get_queueing() << " " << seqno << endl;
*/
        _src->_found_reorder++;
    }
    last_ts = fts;
    last_hops = p->get_crthop();
    last_queueing = p->get_queueing();
    last_seqno = seqno;
    packetid_t pktid = p->id();
    /*
    if (p->get_src() == 578 && p->get_dst() == 163 && seqno == 2873+1) {
        cout << "RECVD " << p->ts()/1E6 << endl;
    }
    */
    p->free();

    _packets+= p->size();

    //cout << "Sink recv seqno " << seqno << " size " << size << endl;

    if (seqno == _cumulative_ack+1) { // it's the next expected seq no
	_cumulative_ack = seqno + size - 1;
	//cout << "New cumulative ack is " << _cumulative_ack << endl;
	// are there any additional received packets we can now ack?
    while (!_received.empty() && (_received.front() == _cumulative_ack+1) ) {
        _src->_top->decr_host_buffer(_src->get_flow_dst());
        _src->buffer_change--;
        _received.pop_front();
        _cumulative_ack+= size;
    }
    //outofseq is solved once all the missing holes have been filled
    if (waiting_for_seq) {
/*
	if(_received.empty() || cons_out_of_seq_n < 3) {
            if(!(fts > out_of_seq_fts)) {
            cout << "OUTOFSEQ " << _src->get_flow_src() << " " << _src->get_flow_dst() << " " << _src->get_flowsize() << " "
                << out_of_seq_fts-fts << " " << _src->eventlist().now()-out_of_seq_rxts << " " << seqno << " " << out_of_seq_n << endl;
            }
            waiting_for_seq = false;
            out_of_seq_n = 0;
	    cons_out_of_seq_n = 0;
	} else {
	    //cout << "NOTYETSEQ " << _src->get_flow_src() << " " << _src->get_flow_dst() << " ack " << seqno << " next " << _received.front() << endl;
            out_of_seq_n++;
        }
*/
            if(!(fts > out_of_seq_fts)) {
            cout << "OUTOFSEQ " << _src->get_flow_src() << " " << _src->get_flow_dst() << " " << _src->get_flowsize() << " "
                << out_of_seq_fts-fts << " " << _src->eventlist().now()-out_of_seq_rxts << " " << seqno << " " << out_of_seq_n << endl;
            }
            waiting_for_seq = false;
            out_of_seq_n = 0;
    }
    } else if (seqno < _cumulative_ack+1) {
    } else { // it's not the next expected sequence number
    //check whether the expected seqno was dropped. if not, it's a reorder
    if(!_src->was_it_dropped(_cumulative_ack+1, false)) {
        if(!waiting_for_seq) {
            waiting_for_seq = true;
            out_of_seq_fts = fts;
            out_of_seq_rxts = _src->eventlist().now();
        }
        out_of_seq_n++;
	cons_out_of_seq_n++;
    } else if(waiting_for_seq) {
        //it could have been dropped while arriving late...
        waiting_for_seq = false;
        out_of_seq_n = 0;
	cons_out_of_seq_n = 0;
    }
        /*
        if(_src->get_flow_src() == 578 && _src->get_flow_dst() == 163 && _cumulative_ack+1 == 2873+1) {
            cout << "EXPECTING 2874 GOT " << seqno << " " << ts/1E6 << endl;
        }
        */
	if (_received.empty()) {
            _src->_top->inc_host_buffer(_src->get_flow_dst());
            _src->buffer_change++;
	    _received.push_front(seqno);
	    //it's a drop in this simulator there are no reorderings.
	    _drops += (1000 + seqno-_cumulative_ack-1)/1000;
	} else if (seqno > _received.back()) { // likely case
	    _received.push_back(seqno);
        _src->_top->inc_host_buffer(_src->get_flow_dst());
        _src->buffer_change++;
	} else { // uncommon case - it fills a hole
	    list<uint64_t>::iterator i;
	    for (i = _received.begin(); i != _received.end(); i++) {
		if (seqno == *i) break; // it's a bad retransmit
		if (seqno < (*i)) {
            _src->_top->inc_host_buffer(_src->get_flow_dst());
            _src->buffer_change++;
		    _received.insert(i, seqno);
		    break;
		}
	    }
	}
    }
    send_ack(fts,marked,pktslice,used_path);
}

Queue* 
TcpSink::sendToNIC(Packet* pkt) {
    DynExpTopology* top = pkt->get_topology();
    Queue* nic = top->get_queue_serv_tor(pkt->get_src()); // returns pointer to nic queue
    assert(nic);
    nic->receivePacket(*pkt); // send this packet to the nic queue
    return nic; // return pointer so NDP source can update send time
}

void 
TcpSink::send_ack(simtime_picosec ts,bool marked, int pktslice, vector<int> path) {
    //terribly ugly but that's how opera people made it...
    //just use the previous tcpsrc as a source, packet will get routed based
    //on the inverted src/sink ids and then be received by the source at the end
    TcpAck *ack = TcpAck::newpkt(_src->_top, _src->_flow, _src->_flow_dst, _src->_flow_src, 
            _src, 0, 0, _cumulative_ack, 0);
    ack->set_tcp_slice(pktslice);

    //set SACKs
    //SACK field is blocks in the format of seqx1-seqy1, seqx2-seqy2,... 
    //indicating blocks of received data
    uint16_t mss = _src->_mss;
    list<pair<TcpAck::seq_t, TcpAck::seq_t>> sacks;
    list<TcpAck::seq_t>::iterator it;
    TcpAck::seq_t front = -1;
    TcpAck::seq_t back = -1; 
    for(it = _received.begin(); it != _received.end(); ++it){
        TcpAck::seq_t seqno = (*it);
        if(front == -1) {
            front = seqno;
            back = seqno+mss;
        //contiguous sequence? then same sack block
        } else if(seqno-back <= 0) {
            back = seqno+mss;
        //break in contiguity? then new sack block
        } else {
            sacks.push_back({front,back});
            front = seqno;
            back = seqno+mss;
        }
    }
    //found at least one block
    if(front != -1) sacks.push_back({front,back});
    ack->set_sack(sacks);
    /*
    if (sacks.size() > 0) cout << "PRODUCED SACKS:\n";
    for(auto p : sacks){
        cout << "SACK " << p.first << "," << p.second << endl;
    }
    for(auto seq : _received){
        cout << "RECVD " << seq << endl;
    }
    */
    //ack->flow().logTraffic(*ack,*this,TrafficLogger::PKT_CREATESEND);
    ack->set_ts(ts);
    if (marked) 
        ack->set_flags(ECN_ECHO);
    else
        ack->set_flags(0);
    ack->set_path(path);

    sendToNIC(ack);
}

////////////////////////////////////////////////////////////////
//  TCP RETRANSMISSION TIMER
////////////////////////////////////////////////////////////////

TcpRtxTimerScanner::TcpRtxTimerScanner(simtime_picosec scanPeriod, EventList& eventlist)
    : EventSource(eventlist,"RtxScanner"), _scanPeriod(scanPeriod) {
    eventlist.sourceIsPendingRel(*this, _scanPeriod);
}

void 
TcpRtxTimerScanner::registerTcp(TcpSrc &tcpsrc) {
    _tcps.push_back(&tcpsrc);
}

void TcpRtxTimerScanner::doNextEvent() {
    simtime_picosec now = eventlist().now();
    tcps_t::iterator i;
    for (i = _tcps.begin(); i!=_tcps.end(); i++) {
	(*i)->rtx_timer_hook(now,_scanPeriod);
    }
    eventlist().sourceIsPendingRel(*this, _scanPeriod);
}

////////////////////////////////////////////////////////////////
//  TCP SACK HANDLER (sender side)
////////////////////////////////////////////////////////////////

TcpSACK::TcpSACK() {
    _dupthresh = 3;
    _mss = 1436;
}

void
TcpSACK::update(uint64_t hiack, list<pair<uint64_t, uint64_t>> sacks, simtime_picosec ts) {
    //assuming receiver has latest scoreboard, so we update ours if we ack is most recent
    if(ts > _latest) {
        _hiack = hiack;
        _scoreboard = sacks;
    }
}

bool
TcpSACK::isLost(uint64_t seqno) {
    list<pair<uint64_t, uint64_t>>::iterator it;
    it = _scoreboard.begin();
    //a packet is lost if either:
    // 1) number of discontiguous sack after its seqno are >= thresh
    // 2) number of sacked segments after its seqno are >= thresh
    int seg_count = 0;
    int seqs_count = 0;
    while(it != _scoreboard.end()) {
        uint64_t low = (*it).first, high = (*it).second; 
        //should not get here...
        if(seqno >= low && seqno < high) {
            assert(0);
            return false;
        }
        //does not count if sacks are previous to seqno
        if(seqno >= high) {
            ++it;
            continue;
        }
        seg_count += high-low;
        seqs_count++;
        //either 1) or 2) is true, then we consider the segment lost
        if(seg_count/_mss >= _dupthresh || seqs_count >= _dupthresh){
            cout << "SACK LOSS " << ((seqs_count>=_dupthresh) ? "SEQS" : "SEGS") << endl;
            for(auto p : _scoreboard){
                cout << "SACK " << p.first << "," << p.second << endl;
            }
            return true;
        }
        ++it;
    }
    return false;
}

bool TcpSACK::isSacked(uint64_t seqno) {
    list<pair<uint64_t, uint64_t>>::iterator it;
    if(seqno < _hiack) return true;
    for(it = _scoreboard.begin(); it != _scoreboard.end(); ++it) {
        uint64_t low = (*it).first, high = (*it).second; 
        if(seqno >= low && seqno < high) return true; 
    } 
    return false;
}

// Returns number of in-flight bytes according to sack subsystem
uint64_t
TcpSACK::setPipe() {
    uint64_t in_flight = 0;
    for(uint64_t seqno = _hiack; seqno <= _hidata+_mss; seqno += _mss) {
        if(isLost(seqno)) {
            in_flight += _mss;
        } 
        if(seqno <= _hirtx){
            in_flight += _mss;
        }
    }
    return in_flight;
}

// Returns next segment to send in case we're in recovery.
// Priority to possibly lost segments in the scoreboard, otherwise send new data
uint64_t
TcpSACK::nextSeg() {
    for(uint64_t seqno = _hiack; seqno <= _hidata+_mss; seqno += _mss) {
        if (isSacked(seqno)) continue;
        uint64_t highest_sack = _scoreboard.back().second;
        if(seqno > _hirtx && seqno < highest_sack && isLost(seqno)) {
            return seqno;
        }
    } 
    return _hidata+1;
}

TcpBulkSender::TcpBulkSender(TcpSrc &src, EventList &eventlist, DynExpTopology *top) 
    : EventSource(eventlist, "TcpBulkSender") {
    _src = &src;
    _top = top;
}

void
TcpBulkSender::doNextEvent() {
   cout << "LONGFLOW " << _src->get_flow_src() << " " << _src->get_flow_dst() << " " <<
       _src->get_flowsize() << " " << _top->time_to_absolute_slice(eventlist().now()) << endl;
   _src->send_packets(); 
   _scheduled = false;
}

void
TcpBulkSender::sendOrSchedule() {
    //return _src->send_packets();
    int src = _src->get_flow_src();
    int dst = _src->get_flow_dst();
    int src_tor = _top->get_firstToR(src);
    int dst_tor = _top->get_firstToR(dst);
    //do not need to schedule if sending within a rack
    if (src_tor == dst_tor) {
        _src->send_packets();
    //else we schedule the sending asap
    } else if (!_scheduled) {
        int slice = _top->time_to_superslice(eventlist().now());
        //uplink,slice
        pair<int,int> r = _top->get_direct_routing(src_tor, dst_tor, slice);
        int sending_slice = r.second;
        assert(sending_slice < _top->get_nsuperslice()*3);
	//can send immediately if it's the right slice
        if(sending_slice == slice) {
            _src->send_packets();
        } else {
            //care wraparound
            int waiting_slices = sending_slice > slice ?
                sending_slice-slice : _top->get_nsuperslice()*3-slice + sending_slice;
            assert(waiting_slices > 0);
            int abs_slice = _top->time_to_absolute_slice(eventlist().now());
            simtime_picosec sending_time = _top->get_slice_start_time(abs_slice+waiting_slices);
            //assert(sending_time > eventlist().now());
            eventlist().sourceIsPending(*this, sending_time);
            _scheduled = true;
        }
    }
}


RTTSampler::RTTSampler(EventList &eventlist, simtime_picosec sample, int src, int dst,
    DynExpTopology *top) : EventSource(eventlist,"rttsampler")
{
    _src = src;
    _dst = dst;
    _sample = sample; 
    _top = top;
    srcSend();
}

void
RTTSampler::startSampling() {
    eventlist().sourceIsPendingRel(*this, _sample);
}

void 
RTTSampler::doNextEvent() {
    srcSend(); 
    eventlist().sourceIsPendingRel(*this, _sample);
}

void 
RTTSampler::srcSend() {
    SamplePacket* p = SamplePacket::newpkt(_top, _src, _dst, this, 64);
    Queue* q = sendToNIC(p);
    assert(q);
}

void
RTTSampler::receivePacket(Packet &p){
    SamplePacket *sp = (SamplePacket*)&p;
    if(p.get_src() == _src) {
        dstRecv(&p);
    } else {
        srcRecv(&p);
    }
}

void
RTTSampler::dstRecv(Packet* p) {
    simtime_picosec ts = p->get_fabricts();
    p->free();
    p = SamplePacket::newpkt(_top, _dst, _src, this, 64);
    ((SamplePacket*)p)->set_ts(ts);
    Queue* q = sendToNIC(p);
    assert(q);
}

void
RTTSampler::srcRecv(Packet* p) {
    simtime_picosec ts = ((SamplePacket*)p)->ts();
    p->free();
    cout << "RTT " << eventlist().now()-ts << " " << eventlist().now() << endl;
}

Queue*
RTTSampler::sendToNIC(Packet* pkt) {
    DynExpTopology* top = pkt->get_topology();
    Queue* nic = top->get_queue_serv_tor(pkt->get_src()); // returns pointer to nic queue
    assert(nic);
    nic->receivePacket(*pkt); // send this packet to the nic queue
    return nic; // return pointer so NDP source can update send time
}
