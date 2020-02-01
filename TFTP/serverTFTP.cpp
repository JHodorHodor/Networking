
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <iostream>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <utility>
#include <boost/program_options.hpp>

using namespace std;
namespace po = boost::program_options;

#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERR 5
#define OACK 6

#define NOT_DEFINED 0
#define FILE_NOT_FOUND 1
#define ACCESS_VIOLATION 2
#define MEM_ERROR 3
#define ILLEGAL_OP 4
#define UNKNOWN_ID 5
#define FILE_EXIST 6
#define NO_SUCH_USER 7

#define DEF_BLOCK_SIZE 512
#define DEF_WINDOW_SIZE 1
#define DEF_TIMEOUT 1

#define SHORT_MAX 65535

int max_block_size;
int max_window_size;
int max_timeout;
int main_socket;
bool only_one_port = false;
bool verbose = false;

struct client {
    sockaddr_in client_addr;
    socklen_t addrlen;
    int _socket;
    unsigned int block_size = DEF_BLOCK_SIZE;
    int window_size = DEF_WINDOW_SIZE;
    int timeouts = DEF_TIMEOUT;
    int fd_read;
    int fd_write;
    int request;
    unsigned short last_ack = 0;
    unsigned short last_write = 0;
    unsigned short last_read;
    int last_block = -1;
    bool done = false;
    time_t next_timeout = -1;
    int overflow = 0;
};
map<int,client> clients;
vector<client> v_clients;

struct message {
    bool correct = true;
    unsigned short opc;
    unsigned  errcode;
    unsigned short errcode_in;
    string filename;
    string mode;
    vector<pair<string,int>> options;
    unsigned short block_nr;
    string data;
    string err_message;
    char* raw;
    int raw_len;
};

void print(client& c){
    cerr << endl;
    cerr << "_socket: " << c._socket << endl;
    cerr << "block_size: " << c.block_size << endl;
    cerr << "window_size: " << c.window_size << endl;
    cerr << "timeouts: " << c.timeouts << endl;
    cerr << "fd_read: " << c.fd_read << endl;
    cerr << "fd_write: " << c.fd_write << endl;
    cerr << "request: " << c.request << endl;
    cerr << "last_ack: " << c.last_ack << endl;
    cerr << "last_write: " << c.last_write << endl;
    cerr << "last_read: " << c.last_read << endl;
    cerr << endl;
}

void print(message& m){
    cerr << endl;
    cerr << "correct: " << m.correct << endl;
    cerr << "opc: " << m.opc << endl;
    cerr << "errcode: " << m.errcode << endl;
    cerr << "errcode_in: " << m.errcode_in << endl;
    cerr << "filename: " << m.filename << endl;
    cerr << "mode: " << m.mode << endl;
    cerr << "block_nr: " << m.block_nr << endl;
    cerr << "data: " << m.data << endl;
    cerr << "err_message: " << m.err_message << endl;
    cerr << "raw_len: " << m.raw_len << endl;
    for(pair<string,int> p : m.options){
        cerr << "opt: " << p.first << p.second << endl;
    }
    cerr << endl;
}

void print_simple(message& m, client& c){

    cerr << c._socket << ": ";
    if(m.opc == RRQ) cerr << "RRQ " << m.filename;
    if(m.opc == WRQ) cerr << "WRQ " << m.filename;
    if(m.opc == ACK) cerr << "ACK " << m.block_nr;
    if(m.opc == DATA) cerr << "DATA " << m.block_nr;
    if(m.opc == ERR) cerr << "ERR ";
    if(m.opc == OACK) cerr << "OACK " << m.block_nr;

    cerr << endl;
}

bool compare_clients(sockaddr_in c1, sockaddr_in c2){
    return (c1.sin_addr.s_addr == c2.sin_addr.s_addr) 
    && (c1.sin_port == c2.sin_port);
}

pair<int,bool> known(sockaddr_in& cl, socklen_t addrlen){
    for(unsigned int i = 0; i < v_clients.size(); i++){
        if(compare_clients(v_clients[i].client_addr,cl))
            return pair<int,bool>(i,false);
    } 

    client c;
    c.client_addr = cl;
    c.addrlen = addrlen;
    c._socket = main_socket;
    v_clients.push_back(c);
    return pair<int,bool>(v_clients.size() - 1,true);
}

int find_min_timeout(){
    int res = -1;
    time_t curr_time = time(NULL);
    if(!only_one_port){
        map<int,client>::iterator it = clients.begin();
        while(it != clients.end()){
            client tmp = it -> second;
            if(tmp.next_timeout != -1 && !tmp.done && tmp.request == RRQ){
                int tmp_res = tmp.next_timeout - curr_time;
                if(tmp_res < 0) tmp_res = 0;
                if(res == -1)
                    res = tmp_res;
                else
                    if(tmp_res < res) 
                        res = tmp_res;
            }
            it++;
        }
    } else {
        for(client tmp : v_clients){
            if(tmp.next_timeout != -1 && !tmp.done && tmp.request == RRQ){
                int tmp_res = tmp.next_timeout - curr_time;
                if(tmp_res < 0) tmp_res = 0;
                if(res == -1)
                    res = tmp_res;
                else
                    if(tmp_res < res) 
                        res = tmp_res;
            }
        }
    }
    return res;
}

bool is_valid_option(string opt){
    if(opt == "timeout" ||
        opt == "blksize" ||
        opt == "windowsize") return true;
    return false;
}

int to_number(string num){
    std::string::const_iterator it = num.begin();
    while (it != num.end() && std::isdigit(*it)) ++it;
    if(!num.empty() && it == num.end()){
        return atoi(num.c_str());
    }
    return -1;
}

int get_next_string(char* buf, int start, int max_size){
    int i = start;
    while(i < max_size + 2 && buf[i] != 0)
        i++;
    if(i == max_size + 2) return -1;
    return i + 1;
}

unsigned short int convert_to_short(char* buf){
    unsigned short int result = 0;
    result += (int)(unsigned char)buf[1];
    result += 256 * (int)(unsigned char)buf[0];
    return result;
}

void put_short(char* buf, unsigned short int val){
    buf[0] = val / 256;
    buf[1] = val;
}

message process_message(char* buf, int max_size, client& c, int rec_len){
    int currpos = 0;
    int last_currpos = 0;
    message m;

    //option
    m.opc = convert_to_short(buf);

    if(m.opc == RRQ || m.opc == WRQ){

        //filename
        currpos = get_next_string(buf, 2, max_size);
        if(currpos == -1) { m.correct = false; m.errcode = ILLEGAL_OP; return m; }
        string tmp1(buf + 2);
        m.filename = tmp1;

        //mode
        last_currpos = currpos;
        currpos = get_next_string(buf, currpos, max_size);
        if(currpos == -1) { m.correct = false; m.errcode = ILLEGAL_OP; return m; }
        string tmp2(buf + last_currpos);
        m.mode = tmp2;

        //options
        pair<string,int> tmp_pair("",-1);
        bool have_opt_name = false;
        while(true){

            last_currpos = currpos;
            currpos = get_next_string(buf, currpos, max_size);
            if(currpos == -1) { return m; }
            string tmp3(buf + last_currpos);
            if(!have_opt_name){
                if(is_valid_option(tmp3)){
                    tmp_pair.first = tmp3;
                    have_opt_name = true;
                }
            } else {
                int val = to_number(tmp3);
                if(val != -1) { 
                    tmp_pair.second = val;
                    m.options.push_back(tmp_pair);
                }
                have_opt_name = false;
            }
        }
        return m;

    }
    
    if(m.opc == DATA){

        m.block_nr = convert_to_short(buf + 2);

        //data
        buf[rec_len] = 0;
        string tmp4(buf + 4);
        m.data = tmp4;
        return m;

    }
    
    if(m.opc == ACK){
    
        m.block_nr = convert_to_short(buf + 2);
        return m;

    }
    
    m.correct = false; 
    m.errcode = ILLEGAL_OP; 
    return m;

}

void option_negotiation(message& m, message& response, client& c){
    response.options.clear();
    for(pair<string,int> op_p : m.options){
        if(op_p.first == "blksize"){
            if(op_p.second <= DEF_BLOCK_SIZE) continue;
            op_p.second = min(op_p.second, max_block_size);
            c.block_size = op_p.second;
            response.options.push_back(op_p);
        } else if(op_p.first == "windowsize"){
            if(op_p.second <= DEF_WINDOW_SIZE) continue;
            op_p.second = min(op_p.second, max_window_size);
            c.window_size = op_p.second;
            response.options.push_back(op_p);
        } else if(op_p.first == "timeout"){
            if(op_p.second <= DEF_TIMEOUT) continue;
            op_p.second = min(op_p.second, max_timeout);
            c.timeouts = op_p.second;
            response.options.push_back(op_p);
        }
    }
}

void insert_string(string& s, char* ptr){
    for(unsigned int i = 0; i < s.length(); i++)
        ptr[i] = s[i];
}

void respond(message& response, client& c){
    char raw[c.block_size + 5];

    if(response.opc == DATA){
        put_short(raw, response.opc);
        put_short(raw + 2, response.block_nr);
        insert_string(response.data, raw + 4);
        int currpos = 4 + response.data.length();
        if(response.data.length() < c.block_size){
            *((char*)(raw + currpos)) = 0;
        }
        response.raw = raw;
        response.raw_len = currpos;
        c.next_timeout = time(NULL) + c.timeouts;
    }

    if(response.opc == ACK){
        put_short(raw, response.opc);
        put_short(raw + 2, response.block_nr);
        response.raw = raw;
        response.raw_len = 4;
    }

    if(response.opc == OACK){
        put_short(raw, response.opc);

        int currpos = 2;
        for(pair<string,int> p_op : response.options){
            insert_string(p_op.first, raw + currpos);
            currpos += p_op.first.length();
            *((char*)(raw + currpos)) = 0;
            currpos++;
            string tmp = to_string(p_op.second);
            insert_string(tmp, raw + currpos);
            currpos += tmp.length();
            *((char*)(raw + currpos)) = 0;
            currpos++;
        }
        
        response.raw = raw;
        response.raw_len = currpos;
    }

    if(response.opc == ERR){
        put_short(raw, response.opc);
        put_short(raw + 2, response.errcode);
        insert_string(response.err_message, raw + 4);
        *((char*)(raw + 4 + response.err_message.length())) = 0;
        response.raw = raw;
        response.raw_len = 4 + response.err_message.length();
    }

    int ret;
    if( (ret = sendto(c._socket, response.raw, response.raw_len, 0, (sockaddr*)&(c.client_addr), c.addrlen)) < 0){
        perror("sendto");
    }
}

void do_and_respond(message& m, client& c){
    message response;
    if(verbose) print_simple(m,c);

    if(!m.correct){ //ERROR
        response.opc = ERR;
        response.errcode = m.errcode;
        respond(response, c);
        return;
    }

    if(m.opc == RRQ){
        int ret;

        if( (ret = open(m.filename.c_str(), O_RDONLY)) == -1 ){
            response.opc = ERR;
            if(errno == EACCES)
                response.errcode = ACCESS_VIOLATION;
            else if(errno == ENOENT)
                response.errcode = FILE_NOT_FOUND;
            else
                response.errcode = NOT_DEFINED; 
            respond(response, c);
            return;         
        }

        c.request = RRQ;
        c.fd_read = ret;
        
        option_negotiation(m, response, c);

        if(response.options.size() > 0){ //some options
            response.opc = OACK;
            respond(response, c);
            return;
        } else {
            m.block_nr = 0;
            m.opc = ACK;
        }

    }

    if(m.opc == WRQ){

        int ret;

        if( (ret = open(m.filename.c_str(), O_WRONLY || O_CREAT)) == -1 ){
            response.opc = ERR;
            if(errno == EACCES)
                response.errcode = ACCESS_VIOLATION;
            else if(errno == EEXIST)
                response.errcode = FILE_EXIST;
            else
                response.errcode = NOT_DEFINED; 
            perror("open");
            respond(response, c);
            return;         
        }

        c.request = WRQ;
        c.fd_write = ret;

        option_negotiation(m, response, c);
        
        c.last_ack = 0;
        if(response.options.size() > 0){ //some options
            response.opc = OACK;
        } else {
            response.opc = ACK;
            response.block_nr = 0;
        }
        
        respond(response, c);
        return;

    }

    if(m.opc == DATA){

        if(c.request != WRQ){
            response.opc = ERR;
            response.errcode = ILLEGAL_OP;
            respond(response, c);
            return;
        }

        if( m.block_nr == c.last_write + 1 ){ //got next block

            int ret;

            if( (ret = write(c.fd_write, m.data.c_str(), m.data.length())) == -1 ){
                response.opc = ERR;
                if(errno == EDQUOT)
                    response.errcode = MEM_ERROR;
                else
                    response.errcode = NOT_DEFINED;
                perror("write");
                respond(response, c);
                return;         
            }

            if(m.data.length() < c.block_size)
                c.done = true;

            c.last_write++;
            if(c.done || c.last_write == c.last_ack + c.window_size){
                response.opc = ACK;
                response.block_nr = c.last_write;
                c.last_ack = c.last_write;
                respond(response,c);
            }
        }
    }

    if(m.opc == ACK){

        if(c.request != RRQ){
            response.opc = ERR;
            response.errcode = ILLEGAL_OP;
            respond(response, c);
            return;
        }

        if(c.last_ack > (unsigned int)m.block_nr) c.overflow++;
        c.last_ack = m.block_nr;

        if(c.last_block != -1 && m.block_nr == c.last_block)
            c.done = true;

        int offset = SHORT_MAX * c.overflow + m.block_nr * c.block_size + 1;
        if(m.block_nr == 0) offset = 0;

        if( lseek(c.fd_read, offset, SEEK_SET) < 0){
            perror("lseek");
            exit(1);
        }

        int ret;
        int rounds = c.window_size;
        int i = m.block_nr + 1;
        while(rounds--){
            if( c.last_block == -1 || (c.last_block != -1 && c.last_block >= i) ){
                response.opc = DATA;
                char buf[c.block_size + 1];
                buf[c.block_size] = 0;
                
                if( (ret = read(c.fd_read, &buf, c.block_size)) == -1 ){
                    perror("read");
                }
                buf[ret] = 0;
                string tmp(buf);
                response.data = tmp;
                response.block_nr = i;
                respond(response, c);

                if(tmp.length() < c.block_size){
                    c.last_block = i;
                    break;
                }
            }
            i++;
        }
    }

}

int create_socket(int port){
    auto sock_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if(sock_fd < 0){
        perror("socket");
        exit(1);
    }

    sockaddr_in name;
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(port); 
    name.sin_addr.s_addr = INADDR_ANY; 

    if ( bind(sock_fd, (sockaddr*)(&name), sizeof(name)) < 0 ) {
        perror("bind");
        exit(1);
    }

    return sock_fd;
}

void old_connection(int sock_fd, int pos){

    client c;
    if(!only_one_port)
        c = clients.find(sock_fd)->second;
    else
        c = v_clients[pos];
    
    int max_size = c.block_size;

    char buf[max_size + 9];
    int reclen = recvfrom(c._socket, buf, max_size + 9, 0, NULL, NULL);

    message m = process_message(buf, max_block_size, c, reclen);
    do_and_respond(m,c);
    
    if(!only_one_port)
        clients.find(sock_fd)->second = c;
    else
        v_clients[pos] = c;
}

int new_connection(int port){

    client c;
        
    sockaddr_in client_addr;
    socklen_t addrlen = sizeof(sockaddr_in);

    char buf[max_block_size + 9];

    //request

    memset(&client_addr, 0, sizeof(client_addr));
    int reclen = recvfrom(main_socket, &buf, max_block_size + 9, 0, (sockaddr*)&client_addr, &addrlen);
    
    c.client_addr = client_addr;
    c.addrlen = addrlen;

    pair<int,bool> ret;
    if(only_one_port){
        ret = known(client_addr, addrlen);
        
        c = v_clients[ret.first];
        if(!ret.second){ //old_client
            old_connection(-1, ret.second);
        }

    } else {
        int sock_fd = create_socket(port);
        c._socket = sock_fd;
    }

    cerr << "CLIENT ON SOCKET: " << c._socket << endl;

    message m = process_message(buf, max_block_size, c, reclen);

    do_and_respond(m,c);
    c.next_timeout = time(NULL) + max_timeout;

    if(!only_one_port)
        clients.insert({c._socket,c});
    else
        v_clients[ret.first] = c;

    return c._socket;
}

void client_timeout(client& c, int sock_fd, int pos){
    if(verbose) cerr << c._socket << "timeout - artificial ACK" << endl;
    message m;
    m.opc = ACK;
    m.block_nr = c.last_ack;
    do_and_respond(m,c);

    if(!only_one_port)
        clients.find(sock_fd)->second = c;
    else
        v_clients[pos] = c;
}

void manage_timeouts(){
    time_t curr_time = time(NULL);
    if(!only_one_port){
        map<int,client>::iterator it = clients.begin();
        while(it != clients.end()){
            client tmp = it -> second;
            if(tmp.next_timeout < curr_time && !tmp.done && tmp.request == RRQ)
                client_timeout(tmp, tmp._socket, -1);
            it++;
        }
    } else {
        for(auto i = 0u; i < v_clients.size(); i++){
            client tmp = v_clients[i];
            if(tmp.next_timeout < curr_time && !tmp.done && tmp.request == RRQ)
                client_timeout(tmp, -1, i);
        }
    }
}

int main(int ac, char** av) {

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "help message")
        ("only_port", "uses only one port (default = TID)")
        ("max_block_size", po::value<int>(), "set max data block size (default_max = 65464, default = 512) (must be <= 512 <= def_max)")
        ("max_window_size", po::value<int>(), "set max window size (default_max = 65535, default = 1) (must be <= 1 <= def_max)")
        ("max_timeout", po::value<int>(), "set max timeout (default_max = 255, default = 1) (must be 1 <= x <= def_max)")
        ("verbose", "print more info")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm); 

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    } 

    if (vm.count("port")) {
        only_one_port = true;
        cerr << "Only port 69 will be used." << ".\n";
    }

    if (vm.count("max_block_size")) {
        max_block_size = max(512, vm["max_block_size"].as<int>());
        cerr << "Max data block set to : " << max_block_size << ".\n";
    } else {
        max_block_size = 65464;
    }

    if (vm.count("max_timeout")) {
        max_timeout = max(512, vm["max_timeout"].as<int>());
        cerr << "Max timeout set to : " << max_timeout << ".\n";
    } else {
        max_timeout = 255;
    }

    if (vm.count("max_window_size")) {
        max_window_size = max(512, vm["max_window_size"].as<int>());
        cerr << "Max window size set to : " << max_window_size << ".\n";
    } else {
        max_window_size = 65535;
    }

    if (vm.count("verbose")) {
        verbose = true;
        cerr << "Verbose mode set" << endl;
    } else {
        verbose = false;
    }


    main_socket = create_socket(6969);
    
    int next_port = 40960;

    int epoll_fd = epoll_create1(0);

    if(epoll_fd < 0){
        perror("epoll_create1");
        exit(1);
    }

    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = main_socket;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, main_socket, &event) == -1){
        perror("epoll_ctl");
        exit(1);
    }
    
    const int MAXEVENTS = 1;
    epoll_event events[MAXEVENTS];

    while ( true ) {

        int t = find_min_timeout();
        if(t != -1) t *= 1000;
        int n = epoll_wait(epoll_fd, events, MAXEVENTS, t);
        manage_timeouts();
        for(int i = 0; i < n; i++){
            if(events[i].events & EPOLLERR || !(events[i].events & EPOLLIN)){
                perror("epoll ERROR");
                exit(1);
            } else if(events[i].data.fd == main_socket){
                int new_fd = new_connection(next_port++);
                if(!only_one_port){
                    event.events = EPOLLIN;
                    event.data.fd = new_fd;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_fd, &event) == -1){
                        perror("epoll_ctl");
                        exit(1);
                    }
                }
            } else {
                old_connection(events[i].data.fd, -1);
            }
        }
    }

    for(auto p : clients){
        close(p.first);
    }

    close(main_socket);
    return 0;
}