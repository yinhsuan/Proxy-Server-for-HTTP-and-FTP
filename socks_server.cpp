//
// async_tcp_echo_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2021 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <array>
#include <fstream>

// using boost::asio::ip::tcp;
using namespace std;
using namespace boost::asio;

class SocksSession : public enable_shared_from_this<SocksSession> {
private:    
    enum { max_length = 1024 };
    unsigned short dstPort;
    string dstIp;
    string _data;
    string _data_c;
    string _data_s;
    string _dnsName;
    ip::tcp::socket socket_c; // client socket
    ip::tcp::socket socket_s; // server socket
    ip::tcp::endpoint endpoint_s;
    int socksOP; // connect: 1/ bind: 2
    ip::address dstAddr;
    ip::tcp::acceptor _acceptor;
    ip::tcp::resolver _resolv;
    

public:
    SocksSession(io_context& io_context_, ip::tcp::socket socket) 
          : socket_c(move(socket)), 
            socket_s(io_context_),
            _acceptor(io_context_),
            _resolv(io_context_),
            _data(max_length, 'x'),
            _data_c(max_length, 'c'),
            _data_s(max_length, 's'),
            _dnsName("") {
    }

    void start() {
        // cout << "Enter start" << endl;
        do_get_socks();
    }

    void do_get_socks() {
        // cout << "Enter do_get_socks" << endl;
        do_read();
    }

    void do_read() {
        // Make sure that connection object outlives the asynchronous operation: 
        // as long as the lambda is alive (i.e. the async. operation is in progress), the connection instance is alive as well.
        auto self(shared_from_this());
        socket_c.async_read_some(boost::asio::buffer(_data, max_length), [this, self](boost::system::error_code ec, size_t length) {
            if (!ec) {
                // SOCKS Request
                socksOP = (unsigned char)_data[1];
                dstPort = (unsigned char)_data[2];
                dstPort <<= 8;
                dstPort +=(unsigned char)_data[3];
                dstIp = "";
                for (int i=4; i<8; i++) {
                    dstIp += to_string((unsigned char)_data[i]);
                    if (i < 7) {
                        dstIp += ".";
                    }
                }
                if ((unsigned char)_data[4] == 0 && (unsigned char)_data[5] == 0 && (unsigned char)_data[6] == 0){
                    int index = 8;
                    do {
                        index++;
                    } while ((unsigned char)_data[index] > 0);

                    _dnsName = "";
                    for (int i=index; i<length-1; i++) {
                        if ((unsigned char)_data[i] == 0) break;
                        _dnsName += _data[i];
                    }
                    // resolve SOCKS Request (_dnsName to ip)
                    do_resolve();
                } 
                else {
                    dstAddr = ip::make_address(dstIp);
                    endpoint_s = ip::tcp::endpoint(dstAddr, dstPort);
                    socks_handle();
                }                
            }
        });
    }

    void do_resolve() {
        auto self(shared_from_this());
        ip::tcp::resolver::query query_s(_dnsName, to_string(dstPort).c_str());
        _resolv.async_resolve(query_s, [this](error_code er, ip::tcp::resolver::results_type results) {
            for (auto& iter: results){
                if (iter.endpoint().address().is_v4()) {
                    endpoint_s = iter.endpoint();
                    break;
                }                    
            }
            socks_handle();
        });
    }

    void socks_handle() {
        bool isToPassFirewall = firewall_check();
        string remoteIp = socket_c.remote_endpoint().address().to_v4().to_string();
        int remotePort = socket_c.remote_endpoint().port();
        string dstIp = endpoint_s.address().to_string();
        int dstPort = endpoint_s.port();

        // Show SOCKS server msg
        cout << "<S_IP>: " << remoteIp << endl;
        cout << "<S_PORT>: " << remotePort << endl;
        cout << "<D_IP>: " << dstIp << endl;
        cout << "<D_PORT>: " << dstPort << endl;

        if (socksOP == 1) {
            cout << "<Command>: CONNECT" << endl;
        } else if (socksOP == 2) {
            cout << "<Command>: BIND" << endl;
        }
        if (isToPassFirewall) {
            cout << "<Reply>: Accept" << endl << endl;
        } else {
            cout << "<Reply>: Reject" << endl << endl;
        }
    
        // SOCKS reply        
        // REJECTED
        if (!isToPassFirewall) {
            string replyMsg = "";
            replyMsg += (char)0;
            replyMsg += (char)91;
            for (int i=0; i<6; i++) {
                replyMsg += (char)0;
            }
            do_reply(replyMsg);
            exit(0);
        }
        // GRANTED
        if (socksOP == 1) {
            do_connect();
        } 
        else if (socksOP == 2) {
            do_bind();
        } 
        else {
            exit(0);
        }    
    }

    void do_bind() {
        ip::tcp::endpoint ep(ip::tcp::v4(), 0);
        _acceptor.open(ep.protocol());
        _acceptor.bind(ep);
        _acceptor.listen();

        int r_port = _acceptor.local_endpoint().port();
        string replyMsg = "";
        replyMsg += (char)0;
        replyMsg += (char)90;
        replyMsg += (char)(r_port/256);
        replyMsg += (char)(r_port%256);
        for (int i=0; i<4; i++) {
            replyMsg += (char)0;
        }
        do_reply(replyMsg);

        auto self(shared_from_this());
        _acceptor.async_accept(socket_s, [this, self, replyMsg](boost::system::error_code ec) {
            if (!ec) {
                do_read_From_Server();
                do_read_From_Client();
                do_reply(replyMsg);
            }
        });
    }

    void do_connect() {
        // cout << "Enter do_connect" << endl;
        string replyMsg = "";
        replyMsg += (char)0;
        replyMsg += (char)90;
        for (int i=0; i<6; i++) {
            replyMsg += (char)0;
        }
        do_reply(replyMsg);

        auto self(shared_from_this());
        socket_s.async_connect(endpoint_s,[this, self](error_code ec){
            if (!ec){
                do_read_From_Server();
                do_read_From_Client();
            } 
        });
    }

    void do_reply(string replyMsg) {
        // cout << "Enter do_reply" << endl;
        socket_c.send(buffer(replyMsg, replyMsg.size()));
    }    

    // ----------------------- To Client ----------------------- //
    void do_read_From_Server() {
        if (!socket_s.is_open()) {
            socket_c.close();
            socket_s.close();
            exit(0);
        }
        auto self(shared_from_this());
        socket_s.async_receive(
            buffer(_data_s, max_length), [this, self](boost::system::error_code ec, size_t length) {
            if (!ec) {
                string _serverData = _data_s.substr(0, length);
                do_send_To_Client(_serverData);
            }
            else {
                socket_c.close();
                socket_s.close();
                exit(0);
            }
        });
    }
    
    void do_send_To_Client(string _serverData) {
        auto self(shared_from_this());
        socket_c.async_send(buffer(_serverData, _serverData.size()), 0, [this, self](error_code ec, size_t length){
            if (!ec){
                do_read_From_Server();
            }
            else{
                socket_c.close();
                socket_s.close();
                exit(0);
            }
        });
    }
    // --------------------------------------------------------- //


    // ----------------------- To Server ----------------------- //
    void do_read_From_Client() { 
        if (!socket_c.is_open()) {
            socket_c.close();
            socket_s.close();
            exit(0);
        }
        auto self(shared_from_this());
        socket_c.async_receive(
            buffer(_data_c, max_length), [this, self](boost::system::error_code ec, size_t length) {
            if (!ec) {
                string _clientData = _data_c.substr(0, length);
                do_send_To_Server(_clientData);
            }
            else {
                socket_c.close();
                socket_s.close();
                exit(0);
            }
        });
    }

    void do_send_To_Server(string _clientData) {
        auto self(shared_from_this());
        socket_s.async_send(
            buffer(_clientData, _clientData.size()), 0, [this, self](error_code ec, size_t length) {
            if (!ec) {
                do_read_From_Client();
            }
            else {
                socket_c.close();
                socket_s.close();
                exit(0);
            }
        });
    }
    // ---------------------------------------------------------- //

    bool firewall_check() {
        // Get dstIp
        string serverIp = endpoint_s.address().to_string();
        int dstIp[4] = {0};
        int index = 0;
        for (int i=0; i<serverIp.size(); i++) {
            if (serverIp[i] == '.') {
                index++;
            }
            else {
                dstIp[index] = dstIp[index]*10 + (serverIp[i]-'0');
            }
        }
        // Compare dstIp with Firewall(socks.conf)
        fstream file;
        file.open("socks.conf", ios::in);
        string s, s1, s2, s3;
        int addrNum = 0;
        index = 0;
        bool flag = true;
        while (getline(file, s)) {
            stringstream ss;
            ss << s;
            ss >> s1 >> s2 >> s3;
            s3 += ".";
            if ((socksOP == 1 && s2[0] != 'c') || (socksOP == 2 && s2[0] != 'b')) {
                continue;
            }
            for (int i=0; i<s3.size(); i++) {
                if (s3[i] == '*') {
                    break;
                } 
                else if (s3[i] == '.') {
                    if (dstIp[index] != addrNum) {
                        flag = false;
                        break;
                    }
                    addrNum = 0;
                    index++;
                } 
                else {
                    addrNum = addrNum*10 + (s3[i]-'0');
                }
            }
        }
        file.close();
        return flag;
    }

    string http_parse(string data){
        stringstream ss(data);
        string queryMethod;
        string uri;
        string requestUri;
        string queryString;
        string serverProtocol;
        string httpHost;
        string drop;

        ss >> queryMethod;
        setenv("REQUEST_METHOD", queryMethod.c_str(), 1);
        
        ss >> uri;
        if ((uri.find("?")) != string::npos) {
            requestUri = uri.substr(0, uri.find("?")); // before ?
            queryString = uri.substr(uri.find("?") + 1); // after ?
        }
        else {
            requestUri = uri;
            queryString = "";
        }
        setenv("REQUEST_URI", uri.c_str(), 1);
        setenv("QUERY_STRING", queryString.c_str(), 1);

        ss >> serverProtocol;
        setenv("SERVER_PROTOCOL", serverProtocol.c_str(), 1);

        ss >> drop >> httpHost;
        setenv("HTTP_HOST", httpHost.c_str(), 1);
        
        return requestUri;
    }    
};

class SocksServer {
private:
    ip::tcp::acceptor acceptor_; 
    ip::tcp::socket socket_;
    signal_set signal_;
    io_context& io_context_;

public:
  SocksServer(boost::asio::io_context& io_context, short port) 
          : acceptor_(io_context, ip::tcp::endpoint(ip::tcp::v4(), port)), // Construct an acceptor opened on the given endpoint
            socket_(io_context),
            signal_(io_context, SIGCHLD),
            io_context_(io_context) {
    signalHandler();
    do_accept();
  }

    void signalHandler() {
        // Start an asynchronous operation to wait for a signal to be delivered
        signal_.async_wait([this](boost::system::error_code ec, int signo) {
            // signal_.cancel() => child will not call signalHandler
            if (acceptor_.is_open()) {
                int status = 0;
                while (waitpid(-1, &status, WNOHANG) > 0) {}
                signalHandler();
            }
        });
  }

    void do_accept() {
        // wait until client connect
        acceptor_.async_accept(socket_, [this](boost::system::error_code ec) {
            if (!ec) {
                io_context_.notify_fork(boost::asio::io_context::fork_prepare);
                if (fork() == 0){
                    io_context_.notify_fork(boost::asio::io_context::fork_child);
                    acceptor_.close(); // child => no need to accept other connection
                    signal_.cancel(); // Cancel all operations associated with the signal set // parent => handle child exit signal
                    make_shared<SocksSession>(io_context_, move(socket_))->start(); // write to client in SocksSession
                }
                else{
                    io_context_.notify_fork(boost::asio::io_context::fork_parent);
                    socket_.close(); // parent => no need to connection to client1
                    do_accept();
                }
            } 
            else {
                do_accept();
            }
        });
    }  
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
        cerr << "Usage: async_tcp_echo_server <port>\n";
        return 1;
        }
        boost::asio::io_context io_context; // io_context => to execute I/O operation
        SocksServer s(io_context, atoi(argv[1])); // port to connect to http_server
        io_context.run(); // if any unfinished async operations => io context::run() will be blocked
    }
    catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}