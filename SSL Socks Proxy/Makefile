.PHONY: all
all: ssl_socks_proxy

ssl_socks_proxy: ssl_socks_proxy.cpp
	g++ -pthread -Wall -g -O2 -std=c++17 ssl_socks_proxy.cpp -lssl -lcrypto -o ssl_socks_proxy
