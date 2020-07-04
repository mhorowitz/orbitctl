PROG = orbitctl
SRCS = orbitctl.cpp
HDRS = uvc.h
CFLAGS = -std=c++11 -O3 $(EXTRA_CFLAGS)
LDFLAGS = -framework IOKit -framework CoreFoundation

$(PROG): $(SRCS) $(HDRS) Makefile
	$(CXX) $(CFLAGS) -o $(PROG) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(PROG)
