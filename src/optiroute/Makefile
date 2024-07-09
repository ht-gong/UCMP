OBJS=eventlist.o pipe.o queue.o loggers.o logfile.o clock.o config.o network.o sent_packets.o rlb.o rlbmodule.o ndp.o ndppacket.o rlbpacket.o compositequeue.o cpqueue.o fairpullqueue.o route.o tcp.o tcppacket.o dctcp.o bolt.o ecnqueue.o boltqueue.o routing.o
HDRS=network.h rlb.h ndp.h compositequeue.h cpqueue.h queue.h loggers.h loggertypes.h pipe.h eventlist.h config.h sent_packets.h ndppacket.h rlbpacket.h rlbmodule.h routing.h tcp.h tcppacket.h dctcp.h bolt.h ecnqueue.h boltqueue.h ecn.h routing_util.h 


CRT=`pwd`
INC= -I/$(CRT)/datacenter -I$(CRT) 

CC=g++
CFLAGS= -Wall -g $(INC)

all:	lib parse_output

lib:	$(OBJS) $(HDRS)
	ar -rvu libhtsim.a $(OBJS)

parse_output: parse_output.o
	$(CC) $(CFLAGS) parse_output.o libhtsim.a -o parse_output 

clean:	
	rm -f *.o htsim htsim_* libhtsim.a parse_output
parse_output.o: parse_output.cpp libhtsim.a
config.o:	config.cpp config.h
eventlist.o:    eventlist.cpp eventlist.h config.h
sent_packets.o:		sent_packets.h sent_packets.cpp
queue.o:	queue.cpp  $(HDRS)
pipe.o:		pipe.cpp $(HDRS)
network.o:	network.cpp  $(HDRS)
fairpullqueue.o:	fairpullqueue.cpp  $(HDRS)
route.o:	route.cpp  $(HDRS)
tcp.o:		tcp.cpp  $(HDRS)
bolt.o:		bolt.cpp  $(HDRS)
dctcp.o:		dctcp.cpp  $(HDRS)
rlb.o:		rlb.cpp $(HDRS)
rlbmodule.o:	rlbmodule.cpp $(HDRS)
routing.o:	  routing.cpp $(HDRS)
ndp.o:		ndp.cpp $(HDRS)
ndplite.o:	ndplite.cpp $(HDRS)
#mtcp.o:		mtcp.cpp $(HDRS)
tcppacket.o:	tcppacket.cpp $(HDRS)
loggers.o:	loggers.cpp $(HDRS)
logfile.o:	logfile.cpp  $(HDRS)
clock.o:	clock.cpp clock.h eventlist.h config.h
compositequeue.o: compositequeue.cpp $(HDRS)
ecnqueue.o:		ecnqueue.cpp $(HDRS)	
boltqueue.o:		boltqueue.cpp $(HDRS)	
#prioqueue.o: prioqueue.cpp $(HDRS)
cpqueue.o: cpqueue.cpp $(HDRS)
#compositeprioqueue.o: compositeprioqueue.cpp $(HDRS)

.cpp.o:
	source='$<' object='$@' libtool=no depfile='$(DEPDIR)/$*.Po' tmpdepfile='$(DEPDIR)/$*.TPo' $(CXXDEPMODE) $(depcomp) $(CC) $(CFLAGS) -Wuninitialized  -c -o $@ `test -f $< || echo '$(srcdir)/'`$<
