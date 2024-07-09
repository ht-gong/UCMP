CC = g++
CFLAGS = -Wall -ggdb -O
CRT=`pwd`
INCLUDE= -I/$(CRT)/.. -I$(CRT) 
LIB=-L..


all:	htsim_ndp_dynexpTopology htsim_tcp_dynexpTopology htsim_dctcp_dynexpTopology


htsim_ndp_dynexpTopology: main_ndp_dynexpTopology.o ../libhtsim.a dynexp_topology.o
	$(CC) $(CFLAGS) main_ndp_dynexpTopology.o dynexp_topology.o $(LIB) -lhtsim -o htsim_ndp_dynexpTopology

htsim_dctcp_dynexpTopology: main_dctcp_dynexpTopology.o ../libhtsim.a dynexp_topology.o
	$(CC) $(CFLAGS) main_dctcp_dynexpTopology.o dynexp_topology.o $(LIB) -lhtsim -o htsim_dctcp_dynexpTopology

htsim_tcp_dynexpTopology: main_tcp_dynexpTopology.o ../libhtsim.a dynexp_topology.o
	$(CC) $(CFLAGS) main_tcp_dynexpTopology.o dynexp_topology.o $(LIB) -lhtsim -o htsim_tcp_dynexpTopology

main_ndp_dynexpTopology.o: main_ndp_dynexpTopology.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_ndp_dynexpTopology.cpp 

main_dctcp_dynexpTopology.o: main_dctcp_dynexpTopology.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_dctcp_dynexpTopology.cpp 

main_tcp_dynexpTopology.o: main_tcp_dynexpTopology.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_tcp_dynexpTopology.cpp 

dynexp_topology.o: dynexp_topology.cpp dynexp_topology.h topology.h
	$(CC) $(INCLUDE) $(CFLAGS) -c dynexp_topology.cpp

clean:	
	rm -f *.o htsim_*
