CXX = g++
CXXFLAGS = $(shell pkg-config --cflags IV)
LDLIBS = $(shell pkg-config --libs IV)

plot: Axis.o Graph.o Plot.o main.o rpc_read.o
	$(CXX) -o $@ $^ $(LDLIBS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o plot
