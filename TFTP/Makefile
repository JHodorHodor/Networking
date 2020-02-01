.PHONY: all
all: clientTFTP serverTFTP

clientTFTP: clientTFTP.cpp
	g++ -pthread -Wall -g -O2 -std=c++17 clientTFTP.cpp -lgsl -lgslcblas -lm -l boost_program_options  -o clientTFTP
serverTFTP: serverTFTP.cpp
	g++ -pthread -Wall -g -O2 -std=c++17 serverTFTP.cpp -lgsl -lgslcblas -lm -l boost_program_options  -o serverTFTP
