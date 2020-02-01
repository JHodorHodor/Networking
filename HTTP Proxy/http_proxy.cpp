#include <httpserver.hpp>
#include <iostream>
#include <map>
#include <curl/curl.h>
#include <string>

using namespace httpserver;
using namespace std;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp){
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static size_t header_callback(void *contents, size_t size, size_t nmemb, void *userp){
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

class proxy : public http_resource {
	public:
        const shared_ptr<http_response> render(const http_request&);
        void set_some_data(const std::string &s) {data = s;}
        std::string data;
};


const shared_ptr<http_response> proxy::render(const http_request& req)
{
    cerr << "GOT REQUEST" << endl;
    auto headers = req.get_headers();
    string host = headers.find("host")->second;

    CURL *handle = curl_easy_init();
    CURLcode res;
    string readBuffer;
    string headerBuffer;

    struct curl_slist *chunk = NULL;
 
    for (const auto& [key, value] : headers) {
        string tmp_k(key);
        string tmp_v(value);
        string tmp = tmp_k + ": " + tmp_v;
        chunk = curl_slist_append(chunk, tmp.c_str());
    }
 
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, chunk);

    curl_easy_setopt(handle, CURLOPT_URL, host.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &headerBuffer);
    res = curl_easy_perform(handle);
    curl_slist_free_all(chunk);
    cout << "CURL: " << res << endl;
    cout << readBuffer << endl;

    auto response = shared_ptr<http_response>(new string_response(readBuffer + "\r\n"));

    int end = headerBuffer.find("\r\n");
    int start = end;
    int found;
    string h_name,h_content;

    cout << "=====" << endl;
    cout << headerBuffer << endl;
    cout << "=====" << endl;
    
    while(true){
        found = headerBuffer.find(" ", end);
        start = found;
        h_name = headerBuffer.substr(end + 2, start - end - 3);
        found = headerBuffer.find("\r\n", start);
        end = found;

        if(found == string::npos) {
            cout << "OK" << endl;
            break;
        }

        h_content = headerBuffer.substr(start + 1, end - start - 1);
        cout << h_name.size() << "--" << h_name << "---->" << h_content << "----" << endl;
        response -> with_header(h_name, h_content);
    }

    return response;
}

int main(int argc, char **argv)
{
    int PORT = atoi(argv[1]);

    webserver ws = create_webserver(PORT).start_method(http::http_utils::INTERNAL_SELECT);

    proxy p;
    ws.register_resource("/", &p, true);

    ws.start(true);
    return 0;
}


//http_proxy=serv_addr:8080
//wget -g -S -O- http://...
//openssl s_client -connect tcp.uj.edu.pl:443