#ifndef TCPPACKET_H
#define TCPPACKET_H

#include <list>
#include "datacenter/dynexp_topology.h"
#include "network.h"
#include "queue.h"



// TcpPacket and TcpAck are subclasses of Packet.
// They incorporate a packet database, to reuse packet objects that are no longer needed.
// Note: you never construct a new TcpPacket or TcpAck directly; 
// rather you use the static method newpkt() which knows to reuse old packets from the database.

#define HEADER_SIZE 64
#define MTU_SIZE 1500

static uint64_t packet_id_gen;

class TcpPacket : public Packet {
public:
	typedef uint64_t seq_t;

	inline static TcpPacket* newpkt(DynExpTopology* top, PacketFlow &flow,
					int flow_src, int flow_dst, TcpSrc *tcp_src, TcpSink *tcp_sink,
                    seq_t seqno, seq_t dataseqno,int size, double priority) {
	    TcpPacket* p = _packetdb.allocPacket();
        p->set_topology(top);
        p->set_src(flow_src);
        p->set_dst(flow_dst);
		p->_crtToR = -1;
		p->_lasthop = false;
		p->_priority = priority;
        p->_tcp_src = tcp_src;
        p->_tcp_sink = tcp_sink;
        p->_flags = 0;
        p->_early_fb = false;
	    p->_type = TCP;
	    p->_seqno = seqno;
	    p->_data_seqno=dataseqno;
	    p->_syn = false;
        p->_size = size+HEADER_SIZE;
        assert(p->_size <= MTU_SIZE);
      p->_id = packet_id_gen++;
	    return p;
	}

	inline static TcpPacket* newpkt(DynExpTopology *top, PacketFlow &flow,
					int flow_src, int flow_dst, TcpSrc *tcp_src, TcpSink *tcp_sink, 
                    seq_t seqno, int size, double priority) {
		return newpkt(top,flow,flow_src,flow_dst,tcp_src,tcp_sink,seqno,0,size, priority);
	}

	inline static TcpPacket* new_syn_pkt(DynExpTopology *top, PacketFlow &flow,
					int flow_src, int flow_dst, TcpSrc *tcp_src, TcpSink *tcp_sink,
                    seq_t seqno, int size) {
		TcpPacket* p = newpkt(top, flow,flow_src,flow_dst,tcp_src,tcp_sink,seqno,0,size, 0);
		p->_syn = true;
		return p;
	}

	void free() {_packetdb.freePacket(this);}
	virtual ~TcpPacket(){}
	inline seq_t seqno() const {return _seqno;}
	inline seq_t data_seqno() const {return _data_seqno;}
	inline simtime_picosec ts() const {return _ts;}
	inline void set_ts(simtime_picosec ts) {_ts = ts;}
    inline void set_tcp_slice(int slice) {_tcp_slice = slice;}
    inline int get_tcp_slice() {return _tcp_slice;}
    virtual inline TcpSrc* get_tcpsrc(){return _tcp_src;}
    virtual inline TcpSink* get_tcpsink(){return _tcp_sink;}
  inline bool last() {return _last_wnd;}
  inline void set_last(bool last) {_last_wnd = last;}
  inline bool first() {return _first_wnd;}
  inline void set_first(bool first) {_first_wnd = first;}
protected:
    TcpSrc *_tcp_src;
    TcpSink *_tcp_sink;
	seq_t _seqno,_data_seqno;
	bool _syn;
    int _tcp_slice;
  bool _last_wnd, _first_wnd;
	simtime_picosec _ts;
	static PacketDB<TcpPacket> _packetdb;
};

class TcpAck : public Packet {
public:
	typedef TcpPacket::seq_t seq_t;
	inline static TcpAck* newpkt(DynExpTopology* top, PacketFlow &flow,
            int flow_src, int flow_dst, TcpSrc *tcp_src, TcpSink *tcp_sink,
			seq_t seqno, seq_t ackno,seq_t dackno) {
	    TcpAck* p = _packetdb.allocPacket();
        p->set_topology(top);
        p->set_src(flow_src);
        p->set_dst(flow_dst);
		p->_priority = 0; 	// Assume control pkts have highest priority
        p->_tcp_src = tcp_src;
        p->_tcp_sink = tcp_sink;
        p->_flags = 0;
        p->_early_fb = false;
	    p->_type = TCPACK;
	    p->_seqno = seqno;
	    p->_ackno = ackno;
	    p->_data_ackno = dackno;
        p->_size = HEADER_SIZE;
      p->_id = packet_id_gen++;
	    return p;
	}

	inline static TcpAck* newpkt(DynExpTopology* top, PacketFlow &flow,
            int flow_src, int flow_dst, TcpSrc *tcp_src, TcpSink *tcp_sink,
			seq_t seqno, seq_t ackno) {
		return newpkt(top,flow,flow_src,flow_dst,tcp_src,tcp_sink,seqno,ackno,0);
	}

	void free() {_packetdb.freePacket(this);}
	inline seq_t seqno() const {return _seqno;}
	inline seq_t ackno() const {return _ackno;}
	inline seq_t data_ackno() const {return _data_ackno;}
	inline simtime_picosec ts() const {return _ts;}
	inline void set_ts(simtime_picosec ts) {_ts = ts;}
    inline void set_tcp_slice(int slice) {_tcp_slice = slice;}
    inline int get_tcp_slice() {return _tcp_slice;}
    void set_sack(list<pair<seq_t, seq_t>> sacks) {_sacks = sacks;}
    list<pair<seq_t, seq_t>> get_sack() {return _sacks;}
    virtual inline TcpSrc* get_tcpsrc(){return _tcp_src;}
    virtual inline TcpSink* get_tcpsink(){return _tcp_sink;}
  void set_bolt_inc(bool inc) {_bolt_inc = inc;}
  bool bolt_inc() {return _bolt_inc;}

	virtual ~TcpAck(){}
	const static int ACKSIZE=40;
protected:
    TcpSrc *_tcp_src;
    TcpSink *_tcp_sink;
	seq_t _seqno;
	seq_t _ackno, _data_ackno;
    int _tcp_slice;
    list<pair<seq_t, seq_t>> _sacks;
	simtime_picosec _ts;
	static PacketDB<TcpAck> _packetdb;
  bool _bolt_inc;
};

#endif
