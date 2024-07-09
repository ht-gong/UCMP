// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef QUEUE_H
#define QUEUE_H

/*
 * A simple FIFO queue
 */

#include <list>
#include "config.h"
#include "dynexp_topology.h"
#include "eventlist.h"
#include "network.h"
#include "loggertypes.h"
#include "routing.h"

class QueueAlarm;
class Routing;
class DynExpTopology;

class Queue : public EventSource, public PacketSink {
 public:
    Queue(linkspeed_bps bitrate, mem_b maxsize, EventList &eventlist, 
	  QueueLogger* logger, Routing* routing);
    Queue(linkspeed_bps bitrate, mem_b maxsize, EventList &eventlist, 
	  QueueLogger* logger, int tor, int port, DynExpTopology *top, Routing *routing);
    virtual void receivePacket(Packet& pkt);
    void doNextEvent();

    void sendFromQueue(Packet* pkt);

    // should really be private, but loggers want to see
    mem_b _maxsize; 
    mem_b _max_recorded_size;
    uint64_t _txbytes;
    vector<mem_b> _max_recorded_size_slice;
    int _tor; // the ToR switch this queue belongs to
    int _port; // the port this queue belongs to
    DynExpTopology* _top; //network topology

    inline simtime_picosec drainTime(Packet *pkt) { 
        return (simtime_picosec)(pkt->size() * _ps_per_byte); 
    }
    inline mem_b serviceCapacity(simtime_picosec t) { 
        return (mem_b)(timeAsSec(t) * (double)_bitrate); 
    }
    virtual mem_b queuesize();
    virtual mem_b slice_queuesize(int slice) = 0;
    virtual simtime_picosec get_queueing_delay(int slice);
    virtual bool isTxing() {return _sending_pkt != NULL;}
    virtual void handleStuck() { return; }
    simtime_picosec get_is_servicing() {return _is_servicing;}
    simtime_picosec get_last_service_time() {return _last_service_begin;}
    simtime_picosec serviceTime();
    int num_drops() const {return _num_drops;}
    void reset_drops() {_num_drops = 0;}

    void reportMaxqueuesize_perslice();
    void reportMaxqueuesize();
    void reportQueuesize();

    virtual void setRemoteEndpoint(Queue* q) {_remoteEndpoint = q;};
    virtual void setRemoteEndpoint2(Queue* q) {_remoteEndpoint = q;q->setRemoteEndpoint(this);};
    Queue* getRemoteEndpoint() {return _remoteEndpoint;}

    virtual void setName(const string& name) {
        Logged::setName(name);
        _nodename += name;
    }
    virtual void setLogger(QueueLogger* logger) {
        _logger = logger;
    }
    virtual const string& nodename() { return _nodename; }

    friend class QueueAlarm;
    friend class Routing;

 protected:
    // Housekeeping
    Queue* _remoteEndpoint;

    QueueLogger* _logger;

    // Mechanism
    // start serving the item at the head of the queue
    virtual void beginService(); 

    // wrap up serving the item at the head of the queue
    virtual void completeService(); 

    void sendEarlyFeedback(Packet *pkt);

    linkspeed_bps _bitrate; 
    simtime_picosec _ps_per_byte;  // service time, in picoseconds per byte
    mem_b _queuesize;
    list<Packet*> _enqueued;
    int _num_drops;
    string _nodename;
    Routing* _routing;
    QueueAlarm* _queue_alarm;
    Packet* _sending_pkt = NULL;
    simtime_picosec _last_service_begin = 0;
    bool _is_servicing = false;
    int _crt_tx_slice = 0;
    
};

/* implement a 4-level priority queue */
// modified to include RLB
class PriorityQueue : public Queue {
 public:
    typedef enum {Q_RLB=0, Q_LO=1, Q_MID=2, Q_HI=3, Q_NONE=4} queue_priority_t;
    PriorityQueue(linkspeed_bps bitrate, mem_b maxsize, EventList &eventlist, 
		  QueueLogger* logger, int node, DynExpTopology *top, Routing* routing);
    virtual void receivePacket(Packet& pkt);
    virtual mem_b queuesize();
    mem_b slice_queuesize(int slice);
    simtime_picosec serviceTime(Packet& pkt);

    void doorbell(bool rlbwaiting);

    int _bytes_sent; // keep track so we know when to send a push to RLB module

    int _node;
    int _crt_slice = -1; // the first packet to be sent will cause a path update

 protected:
    // start serving the item at the head of the queue
    virtual void beginService(); 
    // wrap up serving the item at the head of the queue
    virtual void completeService();

    PriorityQueue::queue_priority_t getPriority(Packet& pkt);
    list <Packet*> _queue[Q_NONE];
    mem_b _queuesize[Q_NONE];
    queue_priority_t _servicing;
    int _state_send;
};

#endif
