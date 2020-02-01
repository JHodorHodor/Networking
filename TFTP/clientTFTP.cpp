
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <iostream>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <map>
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

unsigned int block_size;
int window_size;
int timeout;
int _socket;
string filename;
string your_filename;
int your_fd;
short action;
unsigned short last_read = 0;
unsigned short last_ack = 0;
sockaddr_in server_addr;
string host;
bool done = false;
int last_block = -1;
time_t next_timeout = -1;
bool verbose = false;

struct message {
    bool correct = true;
    unsigned short opc;
    unsigned short errcode;
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

void print_simple(message& m){

    if(m.opc == ACK) cerr << "ACK " << m.block_nr;
    if(m.opc == DATA) cerr << "DATA " << m.block_nr;
    if(m.opc == ERR) cerr << "ERR ";
    if(m.opc == OACK) cerr << "OACK " << m.block_nr;

    cerr << endl;
}

bool is_valid_option(string opt, message& m){
    if(opt == "timeout" ||
        opt == "blksize" ||
        opt == "windowsize") {
            for(pair<string,int> p : m.options){
                if(opt == p.first) return false;
            }
            return true;
        }
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

message build_request(){
    message r;
    r.opc = action;
    r.filename = filename;
    r.mode = "netascii";

    if(block_size != DEF_BLOCK_SIZE){
        r.options.push_back(pair<string,int>("blksize",block_size));
    }
    if(window_size != DEF_WINDOW_SIZE){
        r.options.push_back(pair<string,int>("windowsize", window_size));
    }
    if(timeout != DEF_TIMEOUT){
        r.options.push_back(pair<string,int>("timeout", timeout));
    }
    
    if(r.opc == RRQ){
        if( (your_fd = open(your_filename.c_str(), O_WRONLY || O_CREAT)) < 0 ){
            perror("open");
            exit(1);
        }
    }

    if(r.opc == WRQ){
        if( (your_fd = open(your_filename.c_str(), O_RDONLY)) < 0 ){
            perror("open");
            exit(1);
        }
    }

    return r;
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

message process_message(char* buf, int max_size, int rec_len){
    int currpos = 0;
    int last_currpos = 0;
    message m;

    //option
    m.opc = convert_to_short(buf);
    
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

    if(m.opc == OACK){

        pair<string,int> tmp_pair("",-1);
        currpos = 2;
        bool have_opt_name = false;
        while(true){

            last_currpos = currpos;
            currpos = get_next_string(buf, currpos, max_size);
            if(currpos == -1) { return m; }
            string tmp3(buf + last_currpos);
            if(!have_opt_name){
                if(is_valid_option(tmp3, m)){
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

    if(m.opc == ERR){
        m.errcode = convert_to_short(buf + 2);
        cerr << "ERROR RECEIVED" << endl;
        cerr << m.errcode << endl;
    }
    
    m.correct = false; 
    m.errcode = ILLEGAL_OP; 
    return m;

}

void insert_string(string& s, char* ptr){
    for(unsigned int i = 0; i < s.length(); i++)
        ptr[i] = s[i];
}

void respond(message& response){
    char raw[block_size + 5];

    if(response.opc == RRQ || response.opc == WRQ){
        put_short(raw, response.opc);

        int currpos = 2;
        insert_string(response.filename, raw + currpos);
        currpos += response.filename.length();
        *((char*)(raw + currpos)) = 0;
        currpos++;

        insert_string(response.mode, raw + currpos);
        currpos += response.mode.length();
        *((char*)(raw + currpos)) = 0;
        currpos++;

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

    if(response.opc == DATA){
        put_short(raw, response.opc);
        put_short(raw + 2, response.block_nr);
        insert_string(response.data, raw + 4);
        int currpos = 4 + response.data.length();
        if(response.data.length() < block_size){
            *((char*)(raw + currpos)) = 0;
        }
        response.raw = raw;
        response.raw_len = currpos;
    }

    if(response.opc == ACK){
        put_short(raw, response.opc);
        put_short(raw + 2, response.block_nr);
        response.raw = raw;
        response.raw_len = 4;
        if(response.block_nr >= 100 && response.block_nr < 300) sleep(2); //TEST
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
        cerr << "GOT ERROR FROM SERVER -> EXIT" << endl;
        exit(1);
    }

    if( sendto(_socket, response.raw, response.raw_len, 0, (sockaddr*)&(server_addr), sizeof(server_addr)) < 0){
        perror("sendto");
    }
}

void do_and_respond(message& m){
    message response;
    if(verbose) print_simple(m);

    if(!m.correct){ //ERROR
        response.opc = ERR;
        response.errcode = m.errcode;
        respond(response);
        return;
    }

    if(m.opc == DATA){

        if(action != RRQ){
            response.opc = ERR;
            response.errcode = ILLEGAL_OP;
            respond(response);
            return;
        }


        if( m.block_nr == last_read + 1 ){ //got next block

            int ret;

            if( (ret = write(your_fd, m.data.c_str(), m.data.length())) == -1 ){
                response.opc = ERR;
                if(errno == EDQUOT)
                    response.errcode = MEM_ERROR;
                else
                    response.errcode = NOT_DEFINED; 
                perror("write");
                respond(response);
                return;         
            }

            if(m.data.length() < block_size)
                done = true;

            last_read++;
            if(last_read == last_ack + window_size || done){
                response.opc = ACK;
                response.block_nr = last_read;
                last_ack = last_read;
                respond(response);
            }
        }
    }

    if(m.opc == OACK){

        for(pair<string,int> op_p : m.options){
            if(op_p.first == "blksize"){
                block_size = op_p.second;
            } else if(op_p.first == "windowsize"){
                window_size = op_p.second;
            } else if(op_p.first == "timeout"){
                timeout = op_p.second;
            }
        }

        //for write
        m.block_nr = 0;
        m.opc = ACK; 

        //for read
        if(action == RRQ){
            response.opc = ACK;
            response.block_nr = 0;
            respond(response);
            return;
        }
    }

    if(m.opc == ACK){

        if(action != WRQ){
            response.opc = ERR;
            response.errcode = ILLEGAL_OP;
            respond(response);
            return;
        }

        last_ack = m.block_nr;

        if(last_block == m.block_nr)
            done = true;

        int offset = m.block_nr * block_size + 1;
        if(m.block_nr == 0) offset = 0;

        if( lseek(your_fd, offset, SEEK_SET) < 0){
            perror("lseek");
        }

        int ret;
        int rounds = window_size;
        int i = m.block_nr + 1;
        while(rounds--){
            if( last_block == -1 || (last_block != -1 && last_block >= i) ){
                response.opc = DATA;
                char buf[block_size + 1];
                buf[block_size] = 0;
                
                if( (ret = read(your_fd, &buf, block_size)) == -1 ){
                    perror("read");
                }
                buf[ret] = 0;
                string tmp(buf);
                response.data = tmp;
                response.block_nr = i;
                respond(response);

                if(tmp.length() < block_size){
                    last_block = i;
                    break;
                }
            }
            i++;
        }
    }

}

void manage_timeout(){
    if(next_timeout != -1 && next_timeout <= time(NULL) && action == WRQ){
        message m;
        m.opc = ACK;
        m.block_nr = last_ack;
        do_and_respond(m);
    }
}

int main(int ac, char** av) {

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "help message")
        ("host", po::value<string>(), "set host (default = localhost)")
        ("action", po::value<string>(), "read / write")
        ("filename", po::value<string>(), "set filename")   
        ("your_filename", po::value<string>(), "set your filename(read - dest, write - source")       
        ("block_size", po::value<int>(), "set data block size (default = 512)")
        ("window_size", po::value<int>(), "set window size (default = 1)")
        ("timeout", po::value<int>(), "set timeout (default = 1)")
        ("verbose", "more info")
    
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm); 

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    } 

    if (vm.count("host")) {
        host = vm["host"].as<string>();
        cerr << "Host: " << host << " is set.\n";
    } else {
        host = "localhost";
    }

    if (vm.count("action")) {
        string tmp = vm["action"].as<string>();
        if(tmp == "read"){
            action = RRQ;
        } else if(tmp == "write") {
            action = WRQ;
        } else {
            cerr << "INVALID ACTION" << endl;
            exit(1);
        }
        cerr << "action: " << action << " is set.\n";
    } else {
        cerr << "Use --action." << endl;
        exit(1);
    }

    if (vm.count("filename")) {
        filename = vm["filename"].as<string>();
        cerr << "Filename: " << filename << " is set.\n";
    } else {
        cerr << "Use --filename." << endl;
        exit(1);
    }

    if (vm.count("your_filename")) {
        your_filename = vm["your_filename"].as<string>();
        cerr << "Your filename: " << your_filename << " is set.\n";
    } else {
        cerr << "Use --your_filename." << endl;
        exit(1);
    }

    if (vm.count("block_size")) {
        block_size = vm["block_size"].as<int>();
        cerr << "Block_size: " << block_size << " is set.\n";
    } else {
        block_size = DEF_BLOCK_SIZE;
    }

    if (vm.count("window_size")) {
        window_size = vm["window_size"].as<int>();
        cerr << "Window size: " << window_size << " is set.\n";
    } else {
        window_size = DEF_WINDOW_SIZE;
    }

    if (vm.count("timeout")) {
        timeout = vm["timeout"].as<int>();
        cerr << "Timeout: " << timeout << " is set.\n";
    } else {
        timeout = DEF_TIMEOUT;
    }

    if (vm.count("verbose")) {
        verbose = true;
        cerr << "Verbose mode set" << endl;
    } else {
        verbose = false;
    }

    _socket = socket(AF_INET, SOCK_DGRAM, 0);

    if(_socket < 0){
        perror("socket");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6969);
    struct hostent *sp = gethostbyname(host.c_str());
    memcpy(&server_addr.sin_addr, sp->h_addr, sp->h_length);

    message req = build_request();
    respond(req);

    //receive first answer
    socklen_t _addrlen = sizeof(sockaddr_in);
    sockaddr_in server_tmp;
    memset(&server_tmp, 0, sizeof(server_tmp));
    char buf[DEF_BLOCK_SIZE + 5];
    if(recvfrom(_socket, buf, DEF_BLOCK_SIZE + 4, 0, (sockaddr*)&server_tmp, &_addrlen) < 0){
        perror("recvfrom");
        exit(1);
    }
    server_addr.sin_port = server_tmp.sin_port;
    message m = process_message(buf, DEF_BLOCK_SIZE + 4, 0);
    
    do_and_respond(m);

    int epoll_fd = epoll_create1(0);

    if(epoll_fd < 0){
        perror("epoll_create1");
        exit(1);
    }

    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = _socket;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, _socket, &event) == -1){
        perror("epoll_ctl");
        exit(1);
    }
    
    const int MAXEVENTS = 1;
    epoll_event events[MAXEVENTS];

    while ( true ) {
        
        if(done){
            cerr << "DONE!" << endl;
            exit(0);
        }

        int n = epoll_wait(epoll_fd, events, MAXEVENTS, timeout * 1000);
        manage_timeout();
        for(int i = 0; i < n; i++){
            if(events[i].events & EPOLLERR || !(events[i].events & EPOLLIN)){
                perror("epoll ERROR");
                exit(1);
            } else if(events[i].data.fd == _socket){
                char bf[block_size + 5];
                int rlen = 0;
                if( (rlen = recvfrom(_socket, bf, block_size + 5, 0, NULL, NULL)) < 0){
                    perror("recvfrom");
                    exit(1);
                }
                message m = process_message(bf, block_size + 5, rlen);
                do_and_respond(m);
            }
        }
    }

    close(_socket);
    return 0;
}
