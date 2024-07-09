// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-        
#ifndef COMPOSITE_QUEUE_H
#define COMPOSITE_QUEUE_H

/*
 * A composite queue that transforms packets into headers when there is no space and services headers with priority. 
 */

// !!! NOTE: this one does selective RLB packet dropping.

// !!! NOTE: this has been modified to also include a lower priority RLB queue


#include "datacenter/dynexp_topology.h"
#define QUEUE_INVALID 0
#define QUEUE_RLB 1 // modified
#define QUEUE_LOW 2
#define QUEUE_HIGH 3


#include <list>
#include "queue.h"
#include "config.h"
#include "eventlist.h"
#include "network.h"
#include "loggertypes.h"

class CompositeQueue : public Queue {
 public:
    CompositeQueue(linkspeed_bps bitrate, mem_b maxsize, 
		   EventList &eventlist, QueueLogger* logger, int tor, int port, DynExpTopology *top, Routing* routing);
    virtual void receivePacket(Packet& pkt);
    virtual void doNextEvent();
    // should really be private, but loggers want to see
    vector<mem_b> _queuesize_low, _queuesize_high;
    mem_b _queuesize_rlb; 
    int num_headers() const { return _num_headers;}
    int num_packets() const { return _num_packets;}
    int num_stripped() const { return _num_stripped;}
    int num_bounced() const { return _num_bounced;}
    int num_acks() const { return _num_acks;}
    int num_nacks() const { return _num_nacks;}
    int num_pulls() const { return _num_pulls;}
    virtual mem_b queuesize();
    mem_b slice_queuesize(int slice);
    simtime_picosec get_queueing_delay(int slice);
    bool isTxing() {return (_serv ==  QUEUE_HIGH || _serv == QUEUE_LOW) ;}
    void preemptRLB();
    void handleStuck();
    void rerouteFromCQ(Packet* pkt);
    virtual void setName(const string& name) {
	Logged::setName(name); 
	_nodename += name;
    }
    virtual const string& nodename() { return _nodename; }

    int _tor; // the ToR switch this queue belongs to
    int _port; // the port this queue belongs to

    int _num_packets;
    int _num_headers; // only includes data packets stripped to headers, not acks or nacks
    int _num_acks;
    int _num_nacks;
    int _num_pulls;
    int _num_stripped; // count of packets we stripped
    int _num_bounced;  // count of packets we bounced

 protected:
    // Mechanism
    void beginService(); // start serving the item at the head of the queue
    void completeService(); // wrap up serving the item at the head of the queue
    bool canBeginService(Packet* to_be_sent); //check if beginService can be called in calendarqueue context
    void returnToSender(Packet *pkt);

    int _serv;
    int _ratio_high, _ratio_low, _crt;

    vector<list<Packet*>> _enqueued_low;
    vector<list<Packet*>> _enqueued_high;

    list<Packet*> _enqueued_rlb; // rlb queue
};

#endif
