.PHONY: all
all: http_proxy

http_proxy: http_proxy.cpp
	g++ -pthread -Wall -g -O2 -std=c++17 http_proxy.cpp -lcurl -lgsl -lgslcblas -lhttpserver  -o http_proxy
