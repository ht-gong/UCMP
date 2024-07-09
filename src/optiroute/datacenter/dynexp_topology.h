#ifndef DYNEXP
#define DYNEXP
#include "main.h"
//#include "randomqueue.h"
//#include "pipe.h" // mod
#include "config.h"
#include "loggers.h" // mod
#include "topology.h"
#include "logfile.h" // mod
#include "routing_util.h"
#include "eventlist.h"
#include <ostream>
#include <vector>

#ifndef QT
#define QT
typedef enum {DEFAULT, COMPOSITE, ECN, ECN_EFB, BOLT} queue_type;
#endif

class Queue;
class Pipe;
class Logfile;
class RlbModule;
class Routing;

class DynExpTopology: public Topology{
  public:

  // basic topology elements: pipes, queues, and RlbModules:

  vector<Pipe*> pipes_serv_tor; // vector of pointers to pipe
  vector<Queue*> queues_serv_tor; // vector of pointers to queue

  vector<RlbModule*> rlb_modules; // each host has an rlb module

  vector<vector<Pipe*>> pipes_tor; // matrix of pointers to pipe
  vector<vector<Queue*>> queues_tor; // matrix of pointers to queue

  // these functions are used for label switching
  Pipe* get_pipe_serv_tor(int node) {return pipes_serv_tor[node];}
  Queue* get_queue_serv_tor(int node) {return queues_serv_tor[node];}
  Pipe* get_pipe_tor(int tor, int port) {return pipes_tor[tor][port];}
  Queue* get_queue_tor(int tor, int port) {return queues_tor[tor][port];}

  RlbModule* get_rlb_module(int host) {return rlb_modules[host];}

  simtime_picosec get_slice_time() {return _tot_time;} // picoseconds spent in total
  simtime_picosec get_slice_uptime() {return _connected_time;} // picoseconds spent in total
  simtime_picosec get_logic_slice_time() {return _connected_time / _nlogicportion;} // picosecond of each logic slice's duration
  simtime_picosec get_relative_time(simtime_picosec t);
  int get_opt_slice(int srcToR, int dstToR, int slice, int path_ind, int hop);
  int time_to_slice(simtime_picosec t);
  int absolute_slice_to_slice(int slice);
  int absolute_logic_slice_to_slice(int slice);
  int time_to_absolute_slice(simtime_picosec t);
  int time_to_logic_slice(simtime_picosec t);
  int time_to_absolute_logic_slice(simtime_picosec t);
  simtime_picosec get_slice_start_time(int slice); 
  simtime_picosec get_logic_slice_start_time(int slice);
  bool is_reconfig(simtime_picosec t);
  bool is_reconfig(simtime_picosec t, simtime_picosec addedtime);
  int get_firstToR(int node) {return node / _ndl;}
  int get_lastport(int dst) {return dst % _ndl;}
  bool is_downlink(int port) {return port < _ndl;}
  int get_path_max_hop() {return _paths_maxhop;}
  int get_path_hops(int srcToR, int dstToR, int slice, int path_ind) {return _lbls[srcToR][dstToR][slice][path_ind].size();}

  // defined in source file
  int get_nextToR(int slice, int crtToR, int crtport);
  int get_port(int srcToR, int dstToR, int slice, int path_ind, int hop);
  bool is_last_hop(int port);
  bool port_dst_match(int port, int crtToR, int dst);
  int get_no_paths(int srcToR, int dstToR, int slice);
  int get_rpath_indices(int srcHost, int dstHost, int slice);
  vector<pair<uint64_t, vector<int>>>* get_lb_and_paths(int srcHost, int dstHost, int slice);
  int get_path_indices(int srctor, int dsttor, int srcHost, int dstHost, int logslice, int path_ind);
  int get_no_hops(int srcToR, int dstToR, int slice, int path_ind);
  int get_nslices() {return _nslice;} 
  int get_nlogicslices() {return _nlogicslice;}
  unsigned get_host_buffer(int host);
  void inc_host_buffer(int host);
  void decr_host_buffer(int host);
  void record_packet_reroute(int hops);
  void report_packet_reroute();


  Logfile* logfile;
  EventList* eventlist;
  int failed_links;
  queue_type qt;

  DynExpTopology(mem_b queuesize, Logfile* lg, EventList* ev,queue_type q, 
                                string topfile, Routing* routing);

  DynExpTopology(mem_b queuesize, Logfile* log, EventList* ev, queue_type q, string topfile, 
                Routing* routing, int marking_threshold, simtime_picosec slice_duration);

  void init_network();

  RlbModule* alloc_rlb_module(DynExpTopology* top, int node);

  Queue* alloc_src_queue(DynExpTopology* top, QueueLogger* q, int node);
  Queue* alloc_queue(QueueLogger* q, mem_b queuesize, int tor, int port);
  Queue* alloc_queue(QueueLogger* q, uint64_t speed, mem_b queuesize, int tor, int port);
  pair<int, int> get_direct_routing(int srcToR, int dstToR, int slice); // Direct routing between src and dst ToRs

  void count_queue(Queue*);
  //vector<int>* get_neighbours(int src) {return NULL;};
  int no_of_nodes() const {return _no_of_nodes;} // number of servers
  int no_of_tors() const {return _ntor;} // number of racks
  int no_of_hpr() const {return _ndl;} // number of hosts per rack = number of downlinks
  

 private:
  map<Queue*,int> _link_usage;
  map<int, unsigned> _host_buffers;
  map<int, unsigned> _max_host_buffers;
  vector<int> _reroute_dict;
  void read_params(string topfile);
  void set_params();
  // Tor-to-Tor connections across time
  // indexing: [slice][uplink (indexed from 0 to _ntor*_nul-1)]
  vector<vector<int>> _adjacency;
  // Randomly generated path indices to follow for each (src, dst) host pair: 
  // used in ECMP, VLB, KShortest routing algorithms
  // indexing: [srchost][dsthost][slice]
  vector<vector<vector<int>>> _rpath_indices;
  // label switched paths
  // indexing: [src][dst][slice][path_ind][sequence of switch ports (queues)]
  vector<vector<vector<vector<vector<int>>>>> _lbls;
  // path indexer for Optiroute
  // indexing: [src][dst][slice][path_ind](lower bound of optimal packet size, [path_ind in labels that correspond])
  vector<vector<vector<vector<pair<uint64_t, vector<int>>>>>> _path_indices;
  // optiroute optimal path slices corresponding to path in _lbls
  // indexing: [src][dst][slice][path_ind][sequence of optimal sending slices]
  vector<vector<vector<vector<vector<int>>>>> _optslices;
  // Connected time slices
  // indexing: [src][dst] -> <time_slice, port> where the src-dst ToRs are connected
  vector<vector<vector<pair<int, int>>>> _connected_slices;
  int _ndl, _nul, _ntor, _no_of_nodes; // number down links, number uplinks, number ToRs, number servers
  int _nslice; // number of topologies
  int _nlogicportion = 1; // number of logic slices per physical slice
  int _nlogicslice = 1; // number of logic slices
  simtime_picosec _slice_dur = 0; // slice duration, if specified from cmdline then overrides the duration read from file
  int _marking_thresh = 0; // marking threshold of TCDCP, if specified from cmdline then default is overriden
  Routing* _routing;
  simtime_picosec _connected_time; // duration of one connected topology
  simtime_picosec _reconfig_time;  // time it takes to reconfigure entire topology
  simtime_picosec _tot_time;  // slice time + reconfiguration time
  int64_t _nsuperslice; // number of "superslices" (periodicity of topology)
  int _paths_maxhop = 0; // number of hops in longest path
  mem_b _queuesize; // queue sizes
};

#endif
