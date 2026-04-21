CXX = g++
CXXFLAGS = $(shell pkg-config --cflags IV)
LDLIBS = $(shell pkg-config --libs IV)

sendplot: main.o rpc_write.o
	$(CXX) -o $@ $^ $(LDLIBS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o sendplot
