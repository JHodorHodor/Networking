# Networking

This repo consists of three mini projects I did during Networking course in tcs@UJ.

## TFTP

TFTP client-server communication ([RFC 1350](https://tools.ietf.org/html/rfc1350))

Tools: **epoll, boost**
* Sorcerer’s Apprentice Syndrome fixed.
* TID implemented (may be disabled)
* Option extension ([RFC 2347](https://tools.ietf.org/html/rfc2347))
* Block Size, Window Size ([RFC 2348](https://tools.ietf.org/html/rfc2348), [RFC 7440](https://tools.ietf.org/html/rfc7440))
* Timeouts
*In /TFTP/TFTP tests directory you can find some examples*

## HTTP Proxy

Simple HTTP Proxy

Tools: **libcurl, libhttpserver**
* Can receive http request, send it to server and then return respond to the browser.

Usage:

* In your browser settings change proxy to server and port you will use.
* Invoke: ./http_proxy port_nr
* Try to open some http website.

*Due to cashing browsers do, it is safer to use it in incognito/private mode*

## SSL Socks Proxy

Socks proxy which enables to ssl-certificate exchange.

Usage:
* You will need a server with ssl certificate. In code you will need to replace my certificates with yours.
* Then approprietly modify "test" file and invoke:

openssl  s_client -connect 13.79.146.236:8801 -ign_eof < test

with proper server:port.
