#ifndef PROXYSERVICE_H
#define PROXYSERVICE_H

#include <stdio.h>
#include <time.h>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <Poco/ErrorHandler.h>
#include <Poco/Net/Socket.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerRequestImpl.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPClientSession.h>

using namespace std;
using namespace Poco;
using namespace Poco::Net;

class EH : public ErrorHandler
{
    virtual void exception(const Exception& exc)
    {
        printf("%s: %s\n", exc.what(), exc.message().c_str());
    }

    virtual void exception(const std::exception& exc)
    {
        printf("%s\n", exc.what());
    }

    virtual void exception()
    {
        printf("Unknown exception\n");
    }
};

const int BUFFER_SIZE = 8192;

class ProxyService : public HTTPRequestHandler
{
    HTTPClientSession client;

    static unsigned int hashTag(const string &s)
    {
        unsigned int code = s.length();
        for (int i = 0; i < s.length() / 4; ++i)
        {
            unsigned int a = (s[i * 4] & 0xffu) + ((s[i * 4 + 1] & 0xffu) << 8);
            unsigned int b = (s[i * 4 + 2] & 0xffu) + ((s[i * 4 + 3] & 0xffu) << 8);
            code += a;
            code ^= ((code << 5) ^ b) << 0xb;
            code += code >> 0xb;
        }
        switch (s.length() % 4)
        {
        case 1:
            code += s[s.length() - 1] & 0xffu;
            code ^= code << 0xa;
            code += code >> 1;
            break;
        case 2:
            code += (s[s.length() - 2] & 0xffu) + ((s[s.length() - 1] & 0xffu) << 8);
            code ^= code << 0xb;
            code += code >> 0x11;
            break;
        case 3:
            code += (s[s.length() - 3] & 0xffu) + ((s[s.length() - 2] & 0xffu) << 8);
            code ^= (code ^ ((s[s.length() - 1] & 0xffu) << 2)) << 0x10;
            code += code >> 0xb;
            break;
        }
        code ^= code << 3;
        code += code >> 5;
        code ^= code << 4;
        code += code >> 0x11;
        code ^= code << 0x19;
        code += code >> 6;
        return code;
    }

    static string hexString(unsigned int x)
    {
        char buff[20];
        _snprintf(buff, 16, "%08x", x);
        return string(buff, buff + 8);
    }

    void write(istream &in, ostream &out)
    {
        char buffer[BUFFER_SIZE];
        streamsize sz;
        while (in.good() && out.good())
        {
            in.read(buffer, sizeof(buffer));
            sz = in.gcount();
            if (sz <= 0)
                break;
            if (out.good())
                out.write(buffer, sz);
        }
    }

public:

    ProxyService()
    {
        client.setHost("h0.edu.bj.ie.sogou.com");
        client.setPort(80);
    }

    virtual void handleRequest(HTTPServerRequest& request, HTTPServerResponse& response)
    {
        static const string authToken = "5FA7CF6A1809B3B514FA66A1ECA16FEB/30/78e7ff933a9a3063";

        string timestamp(hexString((unsigned)time(0)));

        string tag(hexString(hashTag(timestamp + request.getHost() + "SogouExplorerProxy")));

        try
        {
            HTTPRequest clientRequest(request.getMethod(), request.getURI(), request.getVersion());
            for (NameValueCollection::ConstIterator i = request.begin(); i != request.end(); ++i)
                clientRequest.set(i->first, i->second);

            clientRequest.set("X-Sogou-Auth", authToken);
            clientRequest.set("X-Sogou-Tag", tag);
            clientRequest.set("X-Sogou-Timestamp", timestamp);
            ostream& os = client.sendRequest(clientRequest);
            os.flush();
            if (toUpper(request.getMethod()) == "POST")
            {
                write(request.stream(), os);
                os.flush();
            }
            HTTPResponse clientResponse;
            istream& is = client.receiveResponse(clientResponse);
            response.setStatusAndReason(clientResponse.getStatus(), clientResponse.getReason());
            for (NameValueCollection::ConstIterator i = clientResponse.begin(); i != clientResponse.end(); ++i){
                response.set(i->first, i->second);
            }
//            std::cout<<"Transfer Encoding:"<<response.getTransferEncoding()<<std::endl;
//            ostringstream oss;
//            oss<<("HTTP/1.1 ");
//            = oss;
            ostream &out = response.send();
            out.flush();
            if (toUpper(request.getMethod()) == "CONNECT" && clientResponse.getStatus() == HTTPResponse::HTTP_OK)
            {
                char buffer[BUFFER_SIZE];
                int sz;
                istream &in = request.stream();

                sz = (int)is.readsome(buffer, sizeof(buffer));
                if (sz > 0)
                {
                    out.write(buffer, sz);
                    out.flush();
                }
                sz = (int)in.readsome(buffer, sizeof(buffer));
                if (sz > 0)
                {
                    os.write(buffer, sz);
                    os.flush();
                }

                StreamSocket s1 = client.socket();
                StreamSocket s2 = ((HTTPServerRequestImpl*)&request)->socket();
                vector<Socket> list, l2, l3;
                while (true)
                {
                    list.clear();
                    list.push_back(s1);
                    list.push_back(s2);
                    Socket::select(list, l2, l3, Timespan(20, 0));
                    if (list.empty())
                        break;
                    if (list[0] == s2)
                    {
                        sz = s2.receiveBytes(buffer, sizeof(buffer));
                        if (sz <= 0)
                            break;
                        s1.sendBytes(buffer, sz);
                    }
                    else
                    {
                        sz = s1.receiveBytes(buffer, sizeof(buffer));
                        if (sz <= 0)
                            break;
                        s2.sendBytes(buffer, sz);
                    }
                }
            }
            else
            {
                write(is, out);
                out.flush();
            }
        }
        catch (const Exception &)
        {
            client.reset();
        }
    }
};

class ProxyRequestHandlerFactory : public HTTPRequestHandlerFactory
{
public:
    virtual HTTPRequestHandler *createRequestHandler(const HTTPServerRequest & request)
    {
        return new ProxyService;
    }
};

class SogouProxyServer{
public:
    SogouProxyServer(){ server = NULL;}
    ~SogouProxyServer(){ if(server) delete server;}
    void setProxyPort(int _port){ port = _port;}
    void start()
    {
        if (server == NULL){
            ServerSocket socket;
            socket.bind(SocketAddress("127.0.0.1", port), true);
            socket.listen();
            HTTPServerParams *params = new HTTPServerParams;
            params->setMaxThreads(64);
            params->setServerName("ProxyService");
            params->setSoftwareVersion("1.0");

            server = new HTTPServer(new ProxyRequestHandlerFactory(), socket, params);
        }
        server->start();
    }
    void stop(){ server->stop();}
private:
    int         port;
    HTTPServer  *server;
};

#endif // PROXYSERVICE_H
