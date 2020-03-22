
all: vpngate-connect

vpngate-connect: vpngate-connect.cpp
	$(CXX) -O3 -o $@ -std=c++17 $< -lboost_program_options -lboost_filesystem

clean:
	rm -f vpngate-connect
