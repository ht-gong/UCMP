// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "dynexp_topology.h"
#include <vector>
#include "string.h"
#include <sstream>
#include <strstream>
#include <iostream>
#include <fstream> // to read from file
#include "main.h"
#include "queue.h"
#include "pipe.h"
#include "compositequeue.h"
#include "ecnqueue.h"
#include "ecn.h"
//#include "prioqueue.h"

#include "rlbmodule.h"

extern uint32_t delay_host2ToR; // nanoseconds, host-to-tor link
extern uint32_t delay_ToR2ToR; // nanoseconds, tor-to-tor link

string ntoa(double n);
string itoa(uint64_t n);

DynExpTopology::DynExpTopology(mem_b queuesize, Logfile* lg, EventList* ev,queue_type q, string topfile){
    _queuesize = queuesize;
    logfile = lg;
    eventlist = ev;
    qt = q;

    read_params(topfile);
 
    set_params();

    init_network();
}

int DynExpTopology::uplink_to_tor(int uplink) {
  return uplink / _nul;
}

int DynExpTopology::uplink_to_port(int uplink) {
  int tor = uplink_to_tor(uplink);
  return (uplink - tor * _nul + _ndl);
}

pair<int, int> DynExpTopology::get_direct_routing(int srcToR, int dstToR, int slice) {
  vector<pair<int, int>> connected = _connected_slices[srcToR][dstToR];
  // YX: TODO: change to binary search
  int index = -1;
  for (index = 0; index < connected.size(); index++) {
    if (connected[index].first >= slice) {
      break;
    }
  }
  if (index == connected.size()) {
    index = 0;
  }
  //return uplink,slice
  return make_pair(connected[index].second, connected[index].first);
}

// read the topology info from file (generated in Matlab)
void DynExpTopology::read_params(string topfile) {

  ifstream input(topfile);

  if (input.is_open()){

    // read the first line of basic parameters:
    string line;
    getline(input, line);
    stringstream stream(line);
    stream >> _no_of_nodes;
    stream >> _ndl;
    stream >> _nul;
    stream >> _ntor;

    // get number of topologies
    getline(input, line);
    stream.str(""); stream.clear(); // clear `stream` for re-use in this scope 
    stream << line;
    stream >> _nslice;
    // get picoseconds in each topology slice type
    _slicetime.resize(4);
    stream >> _slicetime[0]; // time spent in "epsilon" slice
    stream >> _slicetime[1]; // time spent in "delta" slice
    stream >> _slicetime[2]; // time spent in "r" slice
    // total time in the "superslice"
    _slicetime[3] = _slicetime[0] + _slicetime[1] + _slicetime[2];

    _nsuperslice = _nslice / 3;

    // get topology
    // format:
    //       uplink -->
    //    ----------------
    // slice|
    //   |  |   (next ToR)
    //   V  |
    int temp;
    _adjacency.resize(_nslice);
    for (int i = 0; i < _nslice; i++) {
      getline(input, line);
      stringstream stream(line);
      for (int j = 0; j < _no_of_nodes; j++) {
        stream >> temp;
        _adjacency[i].push_back(temp);
      }
    }


    // get label switched paths (rest of file)
    _lbls.resize(_ntor);
    for (int i = 0; i < _ntor; i++) {
      _lbls[i].resize(_ntor);
      for (int j = 0; j < _ntor; j++) {
        _lbls[i][j].resize(_nslice);
      }
    }

    _connected_slices.resize(_ntor);
    for (int i = 0; i < _ntor; i++) {
      _connected_slices[i].resize(_ntor);
    }

    for (int i = 0; i < _nslice; i++) {
      for (int j = 0; j < _nul*_ntor; j++) {
        int src_tor = uplink_to_tor(j);
        int dst_tor = _adjacency[i][j];
        if(dst_tor >= 0)
            _connected_slices[src_tor][dst_tor].push_back(make_pair(i, uplink_to_port(j)));
      }
    }

    // debug:
    cout << "Loading topology..." << endl;

    int sz = 0;
    while(!input.eof()) {
      int s, d; // current source and destination tor
      int slice; // which topology slice we're in
      vector<int> vtemp;
      getline(input, line);
      if (line.length() <= 0) continue;
      stringstream stream(line);
      while (stream >> temp)
        vtemp.push_back(temp);
      if (vtemp.size() == 1) { // entering the next topology slice
        slice = vtemp[0];
      }
      else {
        s = vtemp[0]; // current source
        d = vtemp[1]; // current dest
        sz = _lbls[s][d][slice].size();
        _lbls[s][d][slice].resize(sz + 1);
        for (int i = 2; i < vtemp.size(); i++) {
        	_lbls[s][d][slice][sz].push_back(vtemp[i]);
        }
      }
    }

    // debug:
    cout << "Loaded topology." << endl;

  }
}

// set number of possible pipes and queues
void DynExpTopology::set_params() {

    pipes_serv_tor.resize(_no_of_nodes); // servers to tors
    queues_serv_tor.resize(_no_of_nodes);

    rlb_modules.resize(_no_of_nodes);

    pipes_tor.resize(_ntor, vector<Pipe*>(_ndl+_nul)); // tors
    queues_tor.resize(_ntor, vector<Queue*>(_ndl+_nul));
}

RlbModule* DynExpTopology::alloc_rlb_module(DynExpTopology* top, int node) {
    return new RlbModule(top, *eventlist, node); // *** all the other params (e.g. link speed) are HARD CODED in RlbModule constructor
}

Queue* DynExpTopology::alloc_src_queue(DynExpTopology* top, QueueLogger* queueLogger, int node) {
    return new PriorityQueue(speedFromMbps((uint64_t)HOST_NIC), memFromPkt(FEEDER_BUFFER), *eventlist, queueLogger, node, this);
}

Queue* DynExpTopology::alloc_queue(QueueLogger* queueLogger, mem_b queuesize, int tor, int port) {
    return alloc_queue(queueLogger, HOST_NIC, queuesize, tor, port);
}

Queue* DynExpTopology::alloc_queue(QueueLogger* queueLogger, uint64_t speed, mem_b queuesize, int tor, int port) {
    if (qt==COMPOSITE)
        return new CompositeQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, tor, port, this);
    else if (qt==DEFAULT)
        return new Queue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, tor, port, this);
    else if (qt==ECN)
        return new ECNQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, 1500*ECN_K, tor, port, this);
    assert(0);
}

// initializes all the pipes and queues in the Topology
void DynExpTopology::init_network() {
  QueueLoggerSampling* queueLogger;

  // initialize server to ToR pipes / queues
  for (int j = 0; j < _no_of_nodes; j++) { // sweep nodes
    rlb_modules[j] = NULL;
    queues_serv_tor[j] = NULL;
    pipes_serv_tor[j] = NULL;
  }
      
  // create server to ToR pipes / queues / RlbModules
  for (int j = 0; j < _no_of_nodes; j++) { // sweep nodes
    queueLogger = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
    logfile->addLogger(*queueLogger);

    rlb_modules[j] = alloc_rlb_module(this, j);

    queues_serv_tor[j] = alloc_src_queue(this, queueLogger, j);
    ostringstream oss;
    oss << "NICQueue " << j;
    queues_serv_tor[j]->setName(oss.str());
    //queues_serv_tor[j][k]->setName("Queue-SRC" + ntoa(k + j*_ndl) + "->TOR" +ntoa(j));
    //logfile->writeName(*(queues_serv_tor[j][k]));
    pipes_serv_tor[j] = new Pipe(timeFromNs(delay_host2ToR), *eventlist);
    //pipes_serv_tor[j][k]->setName("Pipe-SRC" + ntoa(k + j*_ndl) + "->TOR" + ntoa(j));
    //logfile->writeName(*(pipes_serv_tor[j][k]));
  }

  // initialize ToR outgoing pipes / queues
  for (int j = 0; j < _ntor; j++) // sweep ToR switches
    for (int k = 0; k < _nul+_ndl; k++) { // sweep ports
      queues_tor[j][k] = NULL;
      pipes_tor[j][k] = NULL;
    }

  // create ToR outgoing pipes / queues
  for (int j = 0; j < _ntor; j++) { // sweep ToR switches
    for (int k = 0; k < _nul+_ndl; k++) { // sweep ports
      
      if (k < _ndl) {
        // it's a downlink to a server
        queueLogger = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
        logfile->addLogger(*queueLogger);
        queues_tor[j][k] = alloc_queue(queueLogger, _queuesize, j, k);
        ostringstream oss;
        oss << "CompositeQueue" << j << ":" << k;
        queues_tor[j][k]->setName(oss.str());
        //queues_tor[j][k]->setName("Queue-TOR" + ntoa(j) + "->DST" + ntoa(k + j*_ndl));
        //logfile->writeName(*(queues_tor[j][k]));
        pipes_tor[j][k] = new Pipe(timeFromNs(delay_host2ToR), *eventlist);
        //pipes_tor[j][k]->setName("Pipe-TOR" + ntoa(j)  + "->DST" + ntoa(k + j*_ndl));
        //logfile->writeName(*(pipes_tor[j][k]));
      }
      else {
        // it's a link to another ToR
        queueLogger = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
        logfile->addLogger(*queueLogger);
        queues_tor[j][k] = alloc_queue(queueLogger, _queuesize, j, k);
        ostringstream oss;
        oss << "CompositeQueue" << j << ":" << k;
        queues_tor[j][k]->setName(oss.str());
        //queues_tor[j][k]->setName("Queue-TOR" + ntoa(j) + "->uplink" + ntoa(k - _ndl));
        //logfile->writeName(*(queues_tor[j][k]));
        pipes_tor[j][k] = new Pipe(timeFromNs(delay_ToR2ToR), *eventlist);
        //pipes_tor[j][k]->setName("Pipe-TOR" + ntoa(j)  + "->uplink" + ntoa(k - _ndl));
        //logfile->writeName(*(pipes_tor[j][k]));
      }
    }
  }
}


int DynExpTopology::get_nextToR(int slice, int crtToR, int crtport) {
  int uplink = crtport - _ndl + crtToR*_nul;
  //cout << "Getting next ToR..." << endl;
  //cout << "   uplink = " << uplink << endl;
  //cout << "   next ToR = " << _adjacency[slice][uplink] << endl;
  return _adjacency[slice][uplink];
}

int DynExpTopology::get_port(int srcToR, int dstToR, int slice, int path_ind, int hop) {
  //cout << "Getting port..." << endl;
  //cout << "   Inputs: srcToR = " << srcToR << ", dstToR = " << dstToR << ", slice = " << slice << ", path_ind = " << path_ind << ", hop = " << hop << endl;
  //cout << "   Port = " << _lbls[srcToR][dstToR][slice][path_ind][hop] << endl;
  return _lbls[srcToR][dstToR][slice][path_ind][hop];
}

bool DynExpTopology::is_last_hop(int port) {
  //cout << "Checking if it's the last hop..." << endl;
  //cout << "   Port = " << port << endl;
  if ((port >= 0) && (port < _ndl)) // it's a ToR downlink
    return true;
  return false;
}

bool DynExpTopology::port_dst_match(int port, int crtToR, int dst) {
  //cout << "Checking for port / dst match..." << endl;
  //cout << "   Port = " << port << ", dst = " << dst << ", current ToR = " << crtToR << endl;
  if (port + crtToR*_ndl == dst)
    return true;
  return false;
}

int DynExpTopology::get_no_paths(int srcToR, int dstToR, int slice) {
  int sz = _lbls[srcToR][dstToR][slice].size();
  return sz;
}

int DynExpTopology::get_no_hops(int srcToR, int dstToR, int slice, int path_ind) {
  int sz = _lbls[srcToR][dstToR][slice][path_ind].size();
  return sz;
}

int DynExpTopology::time_to_superslice(simtime_picosec t) {
    int64_t superslice = (t / get_slicetime(3)) %
        get_nsuperslice();
    return superslice;
}

int DynExpTopology::time_to_slice(simtime_picosec t){
    int64_t superslice = (t / get_slicetime(3)) %
        get_nsuperslice();
    // next, get the relative time from the beginning of that superslice
    int64_t reltime = t - superslice*get_slicetime(3) -
        (t / (get_nsuperslice()*get_slicetime(3))) * 
        (get_nsuperslice()*get_slicetime(3));
    int slice; // the current slice
    if (reltime < get_slicetime(0))
        slice = 0 + superslice*3;
    else if (reltime < get_slicetime(0) + get_slicetime(1))
        slice = 1 + superslice*3;
    else
        slice = 2 + superslice*3;
    return slice;
}

int DynExpTopology::time_to_absolute_slice(simtime_picosec t) {
  int64_t absolute_superslice = t / get_slicetime(3);
  simtime_picosec remain_time = t - absolute_superslice * get_slicetime(3);
  if (remain_time < get_slicetime(0)) {
    return absolute_superslice * 3;
  } else if (remain_time < get_slicetime(0) + get_slicetime(1)) {
    return 1 + absolute_superslice * 3;
  } else {
    return 2 + absolute_superslice * 3;
  }
}

simtime_picosec DynExpTopology::get_slice_start_time(int slice) {
  int superslice = slice / 3;
  if (slice % 3 == 0) {
    return superslice * get_slicetime(3);
  } else if (slice % 3 == 1) {
    return superslice * get_slicetime(3) + get_slicetime(0);
  } else {
    return superslice * get_slicetime(3) + get_slicetime(0) + get_slicetime(1);
  }
}


void DynExpTopology::count_queue(Queue* queue){
  if (_link_usage.find(queue)==_link_usage.end()){
    _link_usage[queue] = 0;
  }

  _link_usage[queue] = _link_usage[queue] + 1;
}

unsigned DynExpTopology::get_host_buffer(int host){
    return _host_buffers[host];
}

void DynExpTopology::inc_host_buffer(int host) {
    _host_buffers[host]++;
    if(_host_buffers[host] > _max_host_buffers[host]) {
        _max_host_buffers[host] = _host_buffers[host];
        //cout << "MAXBUF " << host << " " << _max_host_buffers[host] << endl;
    }
}

void DynExpTopology::decr_host_buffer(int host) {
    assert(_host_buffers[host] > 0);
    _host_buffers[host]--;
}
