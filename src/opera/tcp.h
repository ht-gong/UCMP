// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef TCP_H
#define TCP_H

/*
 * A TCP source and sink
 */

#include <cstdint>
#include <list>
#include "config.h"
#include "datacenter/dynexp_topology.h"
#include "network.h"
#include "tcppacket.h"
#include "eventlist.h"
#include "sent_packets.h"
#include "dynexp_topology.h"

//#define MODEL_RECEIVE_WINDOW 1

#define timeInf 0
//#define TDTCP

//#define RANDOM_PATH 1

//#define MAX_SENT 10000

class TcpSink;
class TcpBulkSender;

class TcpSACK {
 public:
    TcpSACK();
    void update(uint64_t hiack, list<pair<uint64_t, uint64_t>> sacks, simtime_picosec ts);
    uint64_t setPipe();
    bool isLost(uint64_t seqno);
    bool isSacked(uint64_t seqno);
    uint64_t nextSeg();

    void updateHiRtx(uint64_t hirtx) {_hirtx = hirtx;}
    void updateHiData(uint64_t hidata) {_hidata = hidata;}
 private:
    uint64_t _hiack, _hidata, _hirtx;
    uint32_t _pipe;
    uint16_t _mss;
    unsigned _dupthresh;
    simtime_picosec _latest;
    list<pair<uint64_t, uint64_t>> _scoreboard;
};

class TcpSrc : public PacketSink, public EventSource {
    friend class TcpSink;
 public:
    TcpSrc(TcpLogger* logger, TrafficLogger* pktlogger, EventList &eventlist, DynExpTopology *top, 
            int flow_src, int flow_dst, bool longflow);
    uint32_t get_id(){ return id;}
    virtual void connect(TcpSink& sink, simtime_picosec startTime);
    void startflow();
    void cleanup();

    void doNextEvent();
    Queue* sendToNIC(Packet* pkt);
    virtual void receivePacket(Packet& pkt);

    //void replace_route(const Route* newroute);

    void set_flowsize(uint64_t flow_size_in_bytes) {
        _flow_size = flow_size_in_bytes;
        if (_flow_size < _mss)
            _pkt_size = _flow_size;
        else
            _pkt_size = _mss;
    }

#ifdef TDTCP
    void set_ssthresh(uint64_t s){
        for(int i = 0; i < _ssthresh.size(); i++)
            _ssthresh[i] = s;
    }
#else
    void set_ssthresh(uint64_t s){_ssthresh = s;}
#endif

    uint32_t effective_window();
    void cmpIdealCwnd(uint64_t ideal_mbps);
    void reportTP();
    virtual void rtx_timer_hook(simtime_picosec now,simtime_picosec period);
    virtual const string& nodename() { return _nodename; }

    inline uint64_t get_flowsize() {return _flow_size;} // bytes
    inline uint64_t get_flowid() {return _flow_id;}
    inline void set_flowid(uint64_t id) {_flow_id = id;}
    inline int get_flow_src() {return _flow_src;}
    inline int get_flow_dst() {return _flow_dst;}
    inline void set_start_time(simtime_picosec startTime) {_start_time = startTime;}
    inline simtime_picosec get_start_time() {return _start_time;};
    void add_to_dropped(uint64_t seqno); //signal dropped seqno
    bool was_it_dropped(uint64_t seqno, bool clear); //check if seqno was dropped. if clear, remove it from list

    // should really be private, but loggers want to see:
    uint64_t _highest_sent;  //seqno is in bytes
    uint64_t _packets_sent;
    uint64_t _flow_size;

    uint32_t _maxcwnd;
    uint16_t _dupacks;
    uint64_t _last_acked;
    //track which tdtcp state had a packet retransmitted
    map<uint64_t, int> _rtx_to_slice;
    //variables that are split between states in TDTCP
#ifdef TDTCP
    vector<uint32_t> _cwnd;
    vector<uint32_t> _ssthresh;
#else
    uint32_t _cwnd;
    uint32_t _ssthresh;
#endif
    uint32_t _old_cwnd;
    uint32_t _old_ssthresh;

    int32_t _app_limited;
    DynExpTopology *_top;
    bool _finished = false;
    uint32_t _found_reorder = 0;
    uint32_t _found_retransmit = 0;
    int buffer_change = 0;

    //round trip time estimate, needed for coupled congestion control
    simtime_picosec _rtt, _rto, _mdev,_base_rtt;
    int _cap;
    simtime_picosec _rtt_avg, _rtt_cum;
    //simtime_picosec when[MAX_SENT];
    int _sawtooth;

    uint16_t _mss;
    uint32_t _unacked; // an estimate of the amount of unacked data WE WANT TO HAVE in the network
    uint32_t _effcwnd; // an estimate of our current transmission rate, expressed as a cwnd
    uint64_t _recoverq;
    uint16_t _pkt_size; // packet size. Equal to _flow_size when _flow_size < _mss. Else equal to _mss
    bool _in_fast_recovery;
    int _fast_recovery_slice; //for TDTCP: network slice that last triggered fast recovery
    simtime_picosec _fast_recovery_start;

    bool _established;

    uint32_t _drops;

    TcpSink* _sink;
    //MultipathTcpSrc* _mSrc;
    simtime_picosec _RFC2988_RTO_timeout;
    bool _rtx_timeout_pending;

    void set_app_limit(int pktps);

    //const Route* _route;
    simtime_picosec _last_ping;
    void send_packets();

	
    int _subflow_id;

#ifdef TDTCP
    virtual void inflate_window(int slice);
    virtual void deflate_window(int slice);
#else
    virtual void inflate_window();
    virtual void deflate_window();
#endif

 protected:
    //const Route* _old_route;
    uint64_t _last_packet_with_old_route;
    vector<uint64_t> _dropped_at_queue;
    vector<pair<TcpAck::seq_t, TcpAck::seq_t>> _sacks;
    TcpSACK _sack_handler;
    TcpBulkSender *_bulk_sender;
    bool _longflow;

    // Housekeeping
    TcpLogger* _logger;
    //TrafficLogger* _pktlogger;

    // Connectivity
    PacketFlow _flow;

    simtime_picosec _start_time;
    uint64_t _flow_id;
    int _flow_src; // the sender (source) for this flow
    int _flow_dst; // the receiver (sink) for this flow

    unsigned _bytes_in_sample = 0;
    simtime_picosec _last_sample = 0;

    // Mechanism
    void clear_timer(uint64_t start,uint64_t end);

    void retransmit_packet();
    int get_crtslice();
    //simtime_picosec _last_sent_time;

    //void clearWhen(TcpAck::seq_t from, TcpAck::seq_t to);
    //void showWhen (int from, int to);
    string _nodename;
};

class TcpSink : public PacketSink, public DataReceiver, public Logged {
    friend class TcpSrc;
 public:
    TcpSink();

    /*
    inline void joinMultipathConnection(MultipathTcpSink* multipathSink){
	_mSink = multipathSink;
    };
    */

    void receivePacket(Packet& pkt);
    Queue* sendToNIC(Packet* pkt);
    TcpAck::seq_t _cumulative_ack; // the packet we have cumulatively acked
    uint64_t _packets;
    uint32_t _drops;
    uint64_t cumulative_ack(){ return _cumulative_ack + _received.size()*1000;}
    uint32_t drops(){ return _src->_drops;}
    uint32_t get_id(){ return id;}
    virtual const string& nodename() { return _nodename; }

    //MultipathTcpSink* _mSink;
    list<TcpAck::seq_t> _received; /* list of packets above a hole, that 
				      we've received */

    TcpSrc* _src;
 private:
    // Connectivity
    uint16_t _crt_path;
    simtime_picosec last_ts = 0;
    unsigned last_hops = 0;
    unsigned last_queueing = 0;
    unsigned last_seqno = 0;

    bool waiting_for_seq = false;
    unsigned out_of_seq_n = 0;
    unsigned cons_out_of_seq_n = 0;
    simtime_picosec out_of_seq_fts = 0;
    simtime_picosec out_of_seq_rxts = 0;
    vector<vector<int>> _used_paths;

    void connect(TcpSrc& src);
    //const Route* _route;

    // Mechanism
    void send_ack(simtime_picosec ts,bool marked,int pktslice, vector<int> path);

    string _nodename;
};

class TcpRtxTimerScanner : public EventSource {
 public:
    TcpRtxTimerScanner(simtime_picosec scanPeriod, EventList& eventlist);
    void doNextEvent();
    void registerTcp(TcpSrc &tcpsrc);
 private:
    simtime_picosec _scanPeriod;
    typedef list<TcpSrc*> tcps_t;
    tcps_t _tcps;
};

class TcpBulkSender : public EventSource {
 public:
    TcpBulkSender(TcpSrc &src, EventList &eventlist, DynExpTopology* top);
    void doNextEvent();
    void sendOrSchedule();
 private:
    TcpSrc* _src;
    DynExpTopology* _top;
    bool _scheduled;
};

class RTTSampler : public EventSource {
 public:
    RTTSampler(EventList &eventlist, simtime_picosec sample, int src, int dst,
        DynExpTopology* top);
    void doNextEvent();
    void startSampling();
    void receivePacket(Packet &pkt);
 private:
    void srcSend();
    void srcRecv(Packet* pkt);
    void dstRecv(Packet* pkt);
    Queue* sendToNIC(Packet* pkt);
    int _src, _dst;
    simtime_picosec _sample;
    DynExpTopology* _top;
};


#endif
