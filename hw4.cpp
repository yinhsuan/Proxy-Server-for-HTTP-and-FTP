#include <boost/asio/io_context.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <array>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>

#define RECV_LEN 1500
#define MAX_CLIENT_NUM 5

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

// connect to np_single_golden
class NpConnect {
    private:        
        io_context& io_context_;
        tcp::resolver resolv_;
        tcp::resolver resolv_socks_;
        tcp::socket socket_;
        tcp::endpoint endpoint_;
        tcp::endpoint endpoint_socks_;
        tcp::resolver::query query_; // get info. about socket
        tcp::resolver::query query_socks_;
        vector<string> commandList_;
        string receiveStr;
        string session_;
        string test_case_;
    public:
        NpConnect(io_context& io_context, const char* hostName, const char* port, const char* proxyName, const char* proxyPort, string test_case, string session)
            : io_context_(io_context),
                resolv_(io_context),
                resolv_socks_(io_context),
                socket_(io_context),
                query_(hostName, port), // (serverHost, serverPort)
                query_socks_(proxyName, proxyPort),
                receiveStr(RECV_LEN, 'x'),
                session_(session),
                test_case_(test_case) {
            commandList_.clear();
        }
        void start() {
                readTestCase();
                do_resolve();
        }
        void readTestCase() {
            fstream file;
            file.open(test_case_, ios::in);
            string command;
            while(getline(file, command)) {
                // LF
                if ((int)(command[command.size()-1]) == 10) {
                    command.erase(command.size()-1);
                } 
                // CR
                if ((int)(command[command.size()-1]) == 13) {
                    command.erase(command.size()-1);
                } 
                commandList_.push_back(command);
            }
        }

        // dns sever => hostname to ip 
        void do_resolve() {
            resolv_.async_resolve(query_, [this](error_code error, tcp::resolver::results_type results) {
                if (!error) {
                    endpoint_ = (*(results.begin())).endpoint();
                    // do_connect();
                    do_resolve_socks();
                }                
            });
        }

        void do_resolve_socks() {
            resolv_socks_.async_resolve(query_socks_, [this](error_code er, tcp::resolver::results_type results){
            endpoint_socks_ = (*(results.begin())).endpoint();
            do_connect();
        });
        }


        // connect to SOCKS server
        void do_connect() {
            socket_.async_connect(endpoint_socks_, [this](error_code error) {
                if (!error) {
                    // do_receive();
                    do_socks_request();
                }
            });
        }

        void do_socks_request() {
            // Make SOCKS Request (dst is np_golden_server)
            string dstIp = endpoint_.address().to_string();
            int dstPort = endpoint_.port();
            string msg = "";
            msg += (unsigned char)4;
            msg += (unsigned char)1;
            msg += (unsigned char)(dstPort/256);
            msg += (unsigned char)(dstPort%256);
            int addr = 0;
            dstIp += ".";
            for (int i=0; i<dstIp.size(); i++) {
                if (dstIp[i] == '.') {
                    msg += (unsigned char)addr;
                    addr = 0;
                }
                else {
                    addr = addr*10 + (dstIp[i]-'0');
                }
            }
            msg += (char)0;
            
            // Send SOCKS Request to SOCKS server
            socket_.async_send(buffer(msg, msg.size()), 0, [this](error_code ec, size_t s) {
                if (!ec) {
                    wait_socks_reply();
                }
                else {
                    cerr << session_ << " Socks request error" << endl;
                    cerr.flush();
                }
            });
        }

        void wait_socks_reply() {
            socket_.async_receive(buffer(receiveStr, RECV_LEN), 0, [this](error_code error, size_t s){
                if (!error) {
                    if ((unsigned short)receiveStr[1] == 90) {
                        do_receive();
                    }
                }
            });
        }


        void do_send() {
            string command = commandList_.front();
            commandList_.erase(commandList_.begin());
            cout << "<script>document.getElementById('" << session_ << "').innerHTML += \"<b>"  << command << "</b>&NewLine;\";</script>";
            cout.flush();
            command += "\n";
            socket_.async_send(buffer(command, command.size()), 0, [this](error_code error, size_t s) {
                if (!error) {
                    do_receive();
                }
            });
        }
        // Receive from NP single golden // and print on browser
        void do_receive() {
            socket_.async_receive(buffer(receiveStr, RECV_LEN), 0, [this](error_code error, size_t s) {
                if (!error) {
                    string result = receiveStr.substr(0, s);
                    string commandResult = "";
                    for (int i=0; i< result.size(); i++) {
                        if (result[i] == '<') {
                            commandResult += "&lt;";
                        } 
                        else if (result[i] == '>') {
                            commandResult += "&gt;";
                        }
                        else if (result[i] == '&') {
                            commandResult += "&amp;";
                        } 
                        else if (result[i] == '\n') {
                            commandResult += "&NewLine;";
                        } 
                        else if (result[i] == '\"') {
                            commandResult += "&quot;";
                        }
                        else {
                            commandResult += result[i];
                        }
                    }                    
                    cout << "<script>document.getElementById('" << session_ << "').innerHTML += \"" << commandResult << "\";</script>\r\n";
                    // here should use flush
                    if ((commandResult.find("% ")) != string::npos) {
                        do_send();
                    }
                    else {
                        do_receive();
                    }
                }
            });
        }
};

map<string, string> parseQuery(string data) {
    map<string, string> result;
    result.clear();
    data += "&";

    while((data.find("&")) != string::npos) {
        int pos = data.find("&");
        string element = data.substr(0, pos);
        data = data.erase(0, pos+1);

        if ((element.find("=")) != string::npos) {
            int pos2 = element.find("=");
            result[element.substr(0, pos2)] = element.substr(pos2+1);
        }
    }
    return result;
}

int main() {    
    boost::asio::io_context io_context;
    map<string, string> queryList;
    if(getenv("QUERY_STRING")) {
        string queryString = string(getenv("QUERY_STRING"));
        queryList = parseQuery(queryString);
    }
    string header1("Content-type: text/html\r\n\r\n\
    <!DOCTYPE html>\
    <html lang=\"en\">\
    <head>\
        <meta charset=\"UTF-8\" />\
        <title>NP Project 3 Console</title>\
        <link\
            rel=\"stylesheet\"\
            href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\"\
            integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\"\
            crossorigin=\"anonymous\"\
        />\
        <link\
            href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\
            rel=\"stylesheet\"\
        />\
        <link\
            rel=\"icon\"\
            type=\"image/png\"\
            href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\
        />\
        <style>\
            * {\
                font-family: \'Source Code Pro\', monospace;\
                font-size: 1rem !important;\
            }\
            body {\
                background-color: #212529;\
            }\
            pre {\
                color: #cccccc;\
            }\
            b {\
                color: #01b468;\
            }\
        </style>\
    </head>\
    <body>\
        <table class=\"table table-dark table-bordered\">\
        <thead>\
            <tr>");
    string header3("</tr>\
        </thead>\
        <tbody>\
            <tr>");
    string header5("</tr>\
        </tbody>\
        </table>\
    </body>\
    </html>");
    string header2;
    string header4;
    NpConnect* np[MAX_CLIENT_NUM];
    for (int i=0; i<MAX_CLIENT_NUM; i++) {
        string sessionId = to_string(i);
        string host = queryList["h"+sessionId];
        string port = queryList["p"+sessionId];
        string proxyHost = queryList["sh"];
        string proxyPort = queryList["sp"];
        string testCase = "test_case/"+queryList["f"+sessionId];
        string session = "s"+sessionId;

        if (queryList["h"+sessionId].size() > 0) {
            header2 += "<th scope=\"col\">";
            header2 += host;
            header2 += ":";
            header2 += port;
            header2 += "</th>";
            header4 += "<td><pre id=\"s";
            header4 += sessionId;
            header4 += "\" class=\"mb-0\"></pre></td>";
            np[i] = new NpConnect(io_context, host.c_str(), port.c_str(), proxyHost.c_str(), proxyPort.c_str(), testCase, session);
            np[i]->start();
        }
    }
    cout << header1 << header2 << header3 << header4 << header5;
    cout.flush();
    io_context.run();
}
