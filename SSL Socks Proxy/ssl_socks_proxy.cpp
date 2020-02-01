#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <thread>
#include <string>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <iostream>
#include <future>
using namespace std;
 
#define BUF_SIZE 4096    /* Buffer for  transfers */


void transfer_S_C(int from, SSL* to){

    char buf[BUF_SIZE];
    int bytes_read;

    while(true){
        bool want_break = false;
        bytes_read = read(from, buf, BUF_SIZE);

        printf("read%d %d--%s--\n",bytes_read,from,buf);
        if (bytes_read == 0) {
            want_break = true;
        } else {
            if (SSL_write(to, buf, bytes_read) == -1) want_break = true;
        }
        if(want_break) break;
    }
}
 
void handle(SSL* client_SSL){
    struct addrinfo hints, *res;
    int server = -1;
 
 
    /* Create the socket */
    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == -1) {
        perror("socket");
        SSL_shutdown(client_SSL);
        SSL_free(client_SSL);
        return;
    }

    char b;
    string remote_host, remote_port;
    bool before_space = true;
    while(true){
        int ret = SSL_read(client_SSL, &b, 1);
        if(ret < 0 ){
            perror("read");
            exit(1);
        }
        if(b == '\n'){
            break;
        } else if(b == '\r'){
            continue;
        } else if(b == ' '){
            before_space = false;
        } else if(before_space){
            remote_host = remote_host + b;
        } else {
            remote_port = remote_port + b;
        }
    }

    /* Get the address info */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(remote_host.c_str(), remote_port.c_str(), &hints, &res) != 0) {
        perror("getaddrinfo");
        SSL_shutdown(client_SSL);
        SSL_free(client_SSL);
        return;
    }


    /* Connect to the host */
    if (connect(server, res->ai_addr, res->ai_addrlen) == -1) {
        perror("connect");
        SSL_shutdown(client_SSL);
        SSL_free(client_SSL);
        return;
    }

    auto future = async(launch::async, transfer_S_C, server, client_SSL);

    int read_blocked;
    char buf[BUF_SIZE];    
    int bytes_read;

    do  {
        bool want_break = false;
        read_blocked = 0;
        bytes_read = SSL_read(client_SSL, buf, BUF_SIZE);

        //check SSL errors
        switch(SSL_get_error(client_SSL, bytes_read)){
            case SSL_ERROR_NONE:
                printf("read%d--%s--\n",bytes_read,buf);
                if (bytes_read == 0) {
                    want_break = true;
                } else {
                    if (write(server, buf, bytes_read) == -1) want_break = true;
                }
            break;
            
            case SSL_ERROR_ZERO_RETURN:     
                want_break = true;
            break;
            
            case SSL_ERROR_WANT_READ:
                read_blocked = 1;
            break;
            
            case SSL_ERROR_WANT_WRITE:
            break;

            default:
                cerr << "READ ERROR" << endl;   
                want_break = true;
        }
        if(want_break) break;
    } while (SSL_pending(client_SSL) && !read_blocked);

    future.wait();
    SSL_shutdown(client_SSL);
    SSL_free(client_SSL);
    close(server);
}

void init_openssl()
{ 
    SSL_load_error_strings();   
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl()
{
    EVP_cleanup();
}

SSL_CTX *create_context()
{
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = SSLv23_server_method();

    ctx = SSL_CTX_new(method);
    if (!ctx) {
    perror("Unable to create SSL context");
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
    }

    return ctx;
}

void configure_context(SSL_CTX *ctx)
{
    SSL_CTX_set_ecdh_auto(ctx, 1);

    /* Set the key and cert */
    if (SSL_CTX_use_certificate_chain_file(ctx, "/etc/letsencrypt/live/azure.jhodor.ninja/fullchain.pem") <= 0) {
        ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "/etc/letsencrypt/live/azure.jhodor.ninja/privkey.pem", SSL_FILETYPE_PEM) <= 0 ) {
        ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    int sock;
    const char *local_port;
 
    /* Get the local and remote hosts and ports from the command line */
    if (argc < 2) {
        fprintf(stderr, "Usage: ./socks_proxy local_port\n");
        return 1;
    }
    local_port = argv[1];


    SSL_CTX *ctx;

    init_openssl();
    ctx = create_context();
    configure_context(ctx);


    /* Create the socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }
 

    sockaddr_in name;
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(atoi(local_port));
    name.sin_addr.s_addr = INADDR_ANY;
    if ( bind(sock, (sockaddr*)(&name), sizeof(name)) < 0 ) {
        perror("bind");
        return 1;
    }
    if ( listen(sock, 16) < 0 ) {
        perror("listen");
        return 1;
    }
  
    /* Main loop */
    while (1) {
        socklen_t size = sizeof(struct sockaddr_in);
        struct sockaddr_in their_addr;
        SSL *ssl;
        int newsock = accept(sock, (struct sockaddr*)&their_addr, &size);
 
        if (newsock == -1) {
            perror("accept");
        } else {
            printf("Got a connection from %s on port %d\n",
                    inet_ntoa(their_addr.sin_addr), htons(their_addr.sin_port));
            
            ssl = SSL_new(ctx);
            SSL_set_fd(ssl, newsock);
            
            if (SSL_accept(ssl) <= 0) {
                ERR_print_errors_fp(stderr);
            }

            std::thread(handle, ssl).detach();
        }


    }
 
    close(sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();
 
    return 0;
}