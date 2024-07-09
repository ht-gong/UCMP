// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "dynexp_topology.h"
#include <vector>
#include <algorithm>
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
#include "boltqueue.h"
#include "ecn.h"
//#include "prioqueue.h"

#include "rlbmodule.h"
extern uint32_t delay_host2ToR; // nanoseconds, host-to-tor link
extern uint32_t delay_ToR2ToR; // nanoseconds, tor-to-tor link

#define OPTIROUTE_ECMP

#define REROUTE_BUCKET_COUNT 1000

string ntoa(double n);
string itoa(uint64_t n);

DynExpTopology::DynExpTopology(mem_b queuesize, Logfile* lg, EventList* ev,queue_type q, 
                                string topfile, Routing* routing):
                                _queuesize(queuesize), logfile(lg), eventlist(ev), qt(q), _routing(routing) {
  
    read_params(topfile);
 
    set_params();

    init_network();
}

DynExpTopology::DynExpTopology(mem_b queuesize, Logfile* lg, EventList* ev,queue_type q, 
                                string topfile, Routing* routing, int marking_threshold, simtime_picosec slice_duration):
                                _queuesize(queuesize), logfile(lg), eventlist(ev), qt(q), _routing(routing), 
                                _slice_dur(slice_duration), _marking_thresh(marking_threshold) {
    

    read_params(topfile);
 
    set_params();

    init_network();
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
    
    stream >> _connected_time;
    _connected_time = (_slice_dur ? _slice_dur : _connected_time); // cmdline duration overrides the one specified in topology file
    
    stream >> _reconfig_time;
    // total time in the "superslice"
    _tot_time = _connected_time + _reconfig_time;

    if(_routing->get_routing_algorithm() == OPTIROUTE) {
      stream >> _nlogicportion;
    }
    _nlogicslice = _nlogicportion * _nslice;

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

    _rpath_indices.resize(_no_of_nodes);
    for (int i = 0; i < _no_of_nodes; i++) {
      _rpath_indices[i].resize(_no_of_nodes);
      for (int j = 0; j < _no_of_nodes; j++) {
        _rpath_indices[i][j].resize(_nslice);
      }
    }

    _lbls.resize(_ntor);
    for (int i = 0; i < _ntor; i++) {
      _lbls[i].resize(_ntor);
      for (int j = 0; j < _ntor; j++) {
        _lbls[i][j].resize(_nlogicslice);
      }
    }

    _path_indices.resize(_ntor);
    for (int i = 0; i < _ntor; i++) {
      _path_indices[i].resize(_ntor);
      for (int j = 0; j < _ntor; j++) {
        _path_indices[i][j].resize(_nlogicslice);
      }
    }

    _optslices.resize(_ntor);
    for (int i = 0; i < _ntor; i++) {
      _optslices[i].resize(_ntor);
      for (int j = 0; j < _ntor; j++) {
        _optslices[i][j].resize(_nlogicslice);
      }
    }

    // calculate connected slices
    _connected_slices.resize(_ntor);
    for (int i = 0; i < _ntor; i++) {
      _connected_slices[i].resize(_ntor);
    }

    for (int i = 0; i < _nslice; i++) {
      for (int j = 0; j < _nul*_ntor; j++) {
        int src_tor = j / _nul;
        int dst_tor = _adjacency[i][j];
        if(dst_tor >= 0)
            _connected_slices[src_tor][dst_tor].push_back(make_pair(i, (j % _nul) + _ndl));
      }
    }

    _reroute_dict.resize(REROUTE_BUCKET_COUNT, 0);

    // debug:
    cout << "Loading topology..." << endl;

    int sz = 0;
    while(!input.eof()) {
      int s, d; // current source and destination tor
      int slice; // which topology slice we're in
      vector<uint64_t> vtemp;
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
        if(_routing->get_routing_algorithm() == OPTIROUTE) {
          for (int i = 3; i < vtemp.size(); i++) {
            _lbls[s][d][slice][sz].push_back(vtemp[i]);
          }
          getline(input, line);
          stringstream stream(line);
          int temp;
          _optslices[s][d][slice].resize(sz + 1);
          while(stream >> temp) {
            _optslices[s][d][slice][sz].push_back(temp);
          }
          uint64_t lb_flowsize = vtemp[2];
          bool found = false;
          for(int path_ind = 0; path_ind < _path_indices[s][d][slice].size(); path_ind++) {
            if(_path_indices[s][d][slice][path_ind].first == lb_flowsize) {
              _path_indices[s][d][slice][path_ind].second.push_back(sz);
              found = true;
              break;
            }
          }
          if(!found) {
            _path_indices[s][d][slice].push_back({lb_flowsize, vector<int>(1, sz)});
          }
        } else {
          for (int i = 2; i < vtemp.size(); i++) {
            _lbls[s][d][slice][sz].push_back(vtemp[i]);
          }
        }
        _paths_maxhop = std::max(_paths_maxhop, (int)_lbls[s][d][slice][sz].size());
      }
    }
    if(_routing->get_routing_algorithm() == OPTIROUTE) {
      for (int i = 0; i < _ntor; i++) {
        for (int j = 0; j < _ntor; j++) {
          for (int k = 0; k < _nlogicslice; k++) {
            std::sort(_path_indices[i][j][k].begin(), _path_indices[i][j][k].end(), [](pair<uint64_t, vector<int>> a, pair<uint64_t, vector<int>> b)
                                    {
                                        return a.first < b.first;
                                    });
          }
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
    return new PriorityQueue(speedFromMbps((uint64_t)HOST_NIC), memFromPkt(FEEDER_BUFFER), *eventlist, queueLogger, node, this, _routing);
}

Queue* DynExpTopology::alloc_queue(QueueLogger* queueLogger, mem_b queuesize, int tor, int port) {
    return alloc_queue(queueLogger, HOST_NIC, queuesize, tor, port);
}

Queue* DynExpTopology::alloc_queue(QueueLogger* queueLogger, uint64_t speed, mem_b queuesize, int tor, int port) {
    if (qt==COMPOSITE)
        return new CompositeQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, tor, port, this, _routing);
    if (qt==BOLT)
        return new BoltQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, tor, port, this, _routing);
    else if (qt==DEFAULT)
        return new CompositeQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, tor, port, this, _routing);
    else if (qt==ECN) {
        if(!_marking_thresh)
          _marking_thresh = DEFAULT_ECN_K;
        ECNQueue *q = new ECNQueue(speedFromMbps(speed), queuesize, *eventlist, queueLogger, 1500*_marking_thresh, tor, port, this, _routing);
        return q;   
    }
    assert(0);
}

// initializes all the pipes and queues in the Topology
void DynExpTopology::init_network() {
  QueueLoggerSampling* queueLogger;

  // pre-samples the path indices for each flow, used for routing
  for (int i = 0; i < _no_of_nodes; i++) {
    for (int j = 0; j < _no_of_nodes; j++) {
      if(get_firstToR(i) == get_firstToR(j)) {
        continue;
      }
      for (int k = 0; k < _nslice; k++) {
         if(_routing->get_routing_algorithm() == ECMP || 
            _routing->get_routing_algorithm() == KSHORTEST) {
          int npaths = get_no_paths(get_firstToR(i), get_firstToR(j), k);
          if (npaths == 0) {
                cout << "Error: there were no paths for slice " << k  << " src " << i <<
                    " dst " << j << endl;
                assert(0);
            }
          _rpath_indices[i][j][k] = rand() % npaths;
        } else if(_routing->get_routing_algorithm() == VLB || 
                  _routing->get_routing_algorithm() == LONGSHORT) {
          _rpath_indices[i][j][k] = rand() % _nul;
        } else if(_routing->get_routing_algorithm() == OPTIROUTE) {
          _rpath_indices[i][j][k] = rand();
        }
      }
    }
  }

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
    pipes_serv_tor[j] = new Pipe(timeFromNs(delay_host2ToR), *eventlist, _routing);
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
        if (qt==COMPOSITE)
          oss << "CompositeQueue";
        else if (qt==DEFAULT)
          oss << "DefaultQueue";
        else if (qt==ECN)
          oss << "ECNQueue";
        oss << j << ":" << k;
        queues_tor[j][k]->setName(oss.str());
        //queues_tor[j][k]->setName("Queue-TOR" + ntoa(j) + "->DST" + ntoa(k + j*_ndl));
        //logfile->writeName(*(queues_tor[j][k]));
        pipes_tor[j][k] = new Pipe(timeFromNs(delay_host2ToR), *eventlist, _routing);
        //pipes_tor[j][k]->setName("Pipe-TOR" + ntoa(j)  + "->DST" + ntoa(k + j*_ndl));
        //logfile->writeName(*(pipes_tor[j][k]));
      }
      else {
        // it's a link to another ToR
        queueLogger = new QueueLoggerSampling(timeFromMs(1000), *eventlist);
        logfile->addLogger(*queueLogger);
        queues_tor[j][k] = alloc_queue(queueLogger, _queuesize, j, k);
        ostringstream oss;
        if (qt==COMPOSITE)
          oss << "CompositeQueue";
        else if (qt==DEFAULT)
          oss << "DefaultQueue";
        else if (qt==ECN)
          oss << "ECNQueue";
        oss << j << ":" << k;
        queues_tor[j][k]->setName(oss.str());
        //queues_tor[j][k]->setName("Queue-TOR" + ntoa(j) + "->uplink" + ntoa(k - _ndl));
        //logfile->writeName(*(queues_tor[j][k]));
        pipes_tor[j][k] = new Pipe(timeFromNs(delay_ToR2ToR), *eventlist, _routing);
        //pipes_tor[j][k]->setName("Pipe-TOR" + ntoa(j)  + "->uplink" + ntoa(k - _ndl));
        //logfile->writeName(*(pipes_tor[j][k]));
      }
    }
  }
}

int DynExpTopology::get_rpath_indices(int srcHost, int dstHost, int slice) {
  return _rpath_indices[srcHost][dstHost][slice];
}

vector<pair<uint64_t, vector<int>>>* DynExpTopology::get_lb_and_paths(int src, int dst, int slice) {
  return &_path_indices[src][dst][slice];
}
int DynExpTopology::get_path_indices(int srctor, int dsttor, int srcHost, int dstHost, int logslice, int path_ind) {
  #ifdef OPTIROUTE_ECMP
    int randnum = _rpath_indices[srcHost][dstHost][logslice / _nlogicportion] % 
      _path_indices[srctor][dsttor][logslice][path_ind].second.size();
    return _path_indices[srctor][dsttor][logslice][path_ind].second[randnum];
  #else
    return _path_indices[srctor][dsttor][logslice][path_ind].second[0];
  #endif
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
  if(_lbls[srcToR][dstToR][slice].size() <= path_ind) {
    cout << _lbls[srcToR][dstToR][slice].size() << " " << path_ind << endl;
  }
  assert(_lbls[srcToR][dstToR][slice].size() > path_ind);
  //assert(_lbls[srcToR][dstToR][slice][path_ind].size() > hop);
  if(_lbls[srcToR][dstToR][slice][path_ind][hop] > 11) {
    cout << "hi :) " << srcToR << " " << dstToR << " " << slice << " " << path_ind << " " << hop << endl;
  }
  return _lbls[srcToR][dstToR][slice][path_ind][hop];
}

int DynExpTopology::get_opt_slice(int srcToR, int dstToR, int slice, int path_ind, int hop) {
  return _optslices[srcToR][dstToR][slice][path_ind][hop];
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
  //cout << "DB " << srcToR << " " << dstToR << " " << slice << " " << path_ind << endl;
  int sz = _lbls[srcToR][dstToR][slice][path_ind].size();
  return sz;
}

// Gets relative time from the start of one slice
simtime_picosec DynExpTopology::get_relative_time(simtime_picosec t) {
  return t % get_slice_time();
}

// Gets relative slice from the start of one cycle
int DynExpTopology::time_to_slice(simtime_picosec t){
  return (t / get_slice_time()) % get_nslices();
}

// Gets absolute slice from start of the simulation
int DynExpTopology::time_to_absolute_slice(simtime_picosec t) {
  return t / get_slice_time();
}

int DynExpTopology::time_to_logic_slice(simtime_picosec t) {
  return time_to_absolute_logic_slice(t) % _nlogicslice;
}

// Gets absolute logic slice from start of the simulation
int DynExpTopology::time_to_absolute_logic_slice(simtime_picosec t) {
  int count = std::min((t % get_slice_time()) / get_logic_slice_time(), (simtime_picosec)_nlogicportion - 1);
  return (t / get_slice_time()) * _nlogicportion + count;
}

// Gets absolute slice from start of the simulation
int DynExpTopology::absolute_slice_to_slice(int slice) {
  return slice % _nslice;
}

int DynExpTopology::absolute_logic_slice_to_slice(int slice) {
  return slice % _nlogicslice;
}

// Gets the starting (absolute) time of a slice 
simtime_picosec DynExpTopology::get_slice_start_time(int slice) {
  return slice * get_slice_time();
}

simtime_picosec DynExpTopology::get_logic_slice_start_time(int slice) {
  return (slice / _nlogicportion) * get_slice_time() + 
         (slice % _nlogicportion) * get_logic_slice_time();
}

bool DynExpTopology::is_reconfig(simtime_picosec t) {
  return get_relative_time(t) > _connected_time;
}

bool DynExpTopology::is_reconfig(simtime_picosec t, simtime_picosec addedtime) {
  return time_to_absolute_slice(t) != time_to_absolute_slice(t + addedtime);
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
        // cout << "MAXBUF " << host << " " << _max_host_buffers[host] << endl;
    }
}

void DynExpTopology::decr_host_buffer(int host) {
    assert(_host_buffers[host] > 0);
    _host_buffers[host]--;
}

void DynExpTopology::record_packet_reroute(int hops) {
  assert(hops < REROUTE_BUCKET_COUNT);
  if(hops > 0) {
    _reroute_dict[hops]++;
  } else {
    _reroute_dict[0]++;
  }
}

void DynExpTopology::report_packet_reroute() {
  cout<<"Reroute Report: \n";
  for(int i = 0; i < REROUTE_BUCKET_COUNT; i++) {
    cout<< i << " " << _reroute_dict[i] << '\n';
  }
}
