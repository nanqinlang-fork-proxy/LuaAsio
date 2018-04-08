#ifdef _WINDOWS
#    include <SDKDDKVer.h>
#
#    ifdef LUAASIO_EXPORTS
#        define DLL_EXPORT __declspec(dllexport)
#    endif
#else
#    define DLL_EXPORT
#endif

#define ASIO_STANDALONE
#define BOOST_DATE_TIME_NO_LIB
#define BOOST_REGEX_NO_LIB
#define ASIO_HAS_BOOST_DATE_TIME

#include <cstdlib>
#include <deque>
#include <map>
#include <iostream>

#include <asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
using asio::ip::tcp;
using namespace std;

#ifndef _WINDOWS
//  #include <sys/types.h>
//  #include <sys/socket.h>
#endif

//--------------------------event----------------------------

const char EVT_ACCEPT = 1;
const char EVT_CONTINUE = 2;

const size_t MAX_EVT_MSG = 10240;

struct event_message {
    char type;
    int dest_id;
    void* source;
    string data;
};
typedef deque<event_message> event_message_queue;
event_message_queue g_evt_queue;

void push_event(char type, int id, void* source, const string &data) {
    event_message evt;
    evt.type = type;
    evt.dest_id = id;
    evt.source = source;
    evt.data = data;
    g_evt_queue.push_back(std::move(evt));

    while (g_evt_queue.size() > MAX_EVT_MSG)
        g_evt_queue.pop_front();
}

//--------------------------client--------------------------

class connection : public boost::enable_shared_from_this<connection> {
private:
    tcp::socket _socket;
    string _read_buff;

private:
    void do_connect(const tcp::resolver::results_type& endpoints, int dest_id) {
        asio::async_connect(_socket, endpoints,
            [this, dest_id](std::error_code ec, tcp::endpoint)
        {
            if (!ec) {
                this->connected = true;
                push_event(EVT_CONTINUE, dest_id, this, "");
            } else {
                push_event(EVT_CONTINUE, dest_id, NULL, ec.message());
            }
        });
    }

public:
    typedef boost::shared_ptr<connection> pointer;
    bool connected;

    connection(asio::io_context& io_context,
        const tcp::resolver::results_type& endpoints, int dest_id)
        : _socket(io_context), connected(false)
    {
        do_connect(endpoints, dest_id);
    }

    connection(tcp::socket socket)
        : _socket(std::move(socket)), connected(true)
    {
    }

    void write(const string& data, int dest_id) {
        auto self = shared_from_this();
        asio::async_write(_socket,
            asio::buffer(data, data.length()),
            [self, dest_id](std::error_code ec, std::size_t /*length*/)
        {
            if (!ec) {
                push_event(EVT_CONTINUE, dest_id, self.get(), "");
            } else {
                self->_socket.close();
                self->connected = false;
                push_event(EVT_CONTINUE, dest_id, NULL, ec.message());
            }
        });
    }

    void read(size_t size, int dest_id) {
        auto self = shared_from_this();
        _read_buff.resize(size);
        asio::async_read(_socket, asio::buffer(_read_buff),
            [self, dest_id](std::error_code ec, std::size_t length)
        {
            if (!ec) {
                push_event(EVT_CONTINUE, dest_id, self.get(), self->_read_buff);
            } else {
                self->_socket.close();
                self->connected = false;
                push_event(EVT_CONTINUE, dest_id, NULL, ec.message());
            }
        });
    }

    void read_some(int dest_id) {
        auto self = shared_from_this();
        const size_t max_buff_size = 1024 * 1024;
        _read_buff.resize(max_buff_size);
        _socket.async_read_some(asio::buffer(_read_buff),
            [self, dest_id](std::error_code ec, std::size_t bytes_transferred)
        {
            if (!ec) {
                self->_read_buff.resize(bytes_transferred);
                push_event(EVT_CONTINUE, dest_id, self.get(), self->_read_buff);
            } else {
                self->_socket.close();
                self->connected = false;
                self->_read_buff.resize(bytes_transferred);
                push_event(EVT_CONTINUE, dest_id, NULL, self->_read_buff);
                push_event(EVT_CONTINUE, dest_id, NULL, ec.message());
            }
        });
    }

    void close() {
        _socket.close();
        connected = false;
    }

    bool get_original_dst(struct sockaddr_storage *destaddr) {
#ifdef _WINDOWS
        return false;
#else
        int size = sizeof(sockaddr_storage);
        int fd   = _socket.native_handle();
        int error = asio::detail::socket_ops::getsockopt(
            fd, SOL_IPV6, IP6T_SO_ORIGINAL_DST, destaddr, &size);
        if (error) {
            error = asio::detail::socket_ops::getsockopt(
                fd, SOL_IP, SO_ORIGINAL_DST, destaddr, &size);
            if (error) {
                return false;
            }
        }
        return true;
#endif
    }

};

//--------------------------server--------------------------

class server{
private:

    tcp::acceptor _acceptor;

private:
    void do_accept() {
        _acceptor.async_accept(
            [this](std::error_code ec, tcp::socket socket)
        {
            if (!ec) {
                auto conn = new boost::shared_ptr<connection>(
                    new connection(std::move(socket)) );
                push_event(EVT_ACCEPT, port, conn, "");
            } else if(ec == asio::error::operation_aborted ) {
                return;
            }

            do_accept();
        });
    }

public:

    int port;

    server(asio::io_context& io_context,
        const asio::ip::address &ip, int port)
        :
        _acceptor(io_context, tcp::endpoint(ip, port)),
        port(port)
    {
        do_accept();
    }

};

//--------------------------api--------------------------

asio::io_context io_context;

extern "C"
DLL_EXPORT void asio_delete_server(void* p) {
    server* svr = (server*)p;
    delete svr;
}

extern "C"
DLL_EXPORT void* asio_new_server(const char* ip, int port) {
    asio::ip::address ip_addr;
    try {
        ip_addr = asio::ip::address::from_string(ip);
    } catch(...) {
        //std::cout << e.what() << std::endl;
        return NULL;
    }

    auto svr = new server(io_context, ip_addr, port);
    return svr;
}

//----------------------

extern "C"
DLL_EXPORT void asio_delete_connection(void* p) {
    auto conn = (connection::pointer*)p;
    delete conn;
}

extern "C"
DLL_EXPORT void* asio_new_connect(const char* host, const char* port, int dest_id) {
    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(host, port);
    auto conn = new boost::shared_ptr<connection>(
        new connection(io_context, endpoints, dest_id));
    return conn;
}

extern "C"
DLL_EXPORT void asio_conn_read(void* p, size_t size, int dest_id) {
    auto conn = (connection::pointer*)p;
    (*conn)->read(size, dest_id);
}

extern "C"
DLL_EXPORT void asio_conn_read_some(void* p, int dest_id) {
    auto conn = (connection::pointer*)p;
    (*conn)->read_some(dest_id);
}

extern "C"
DLL_EXPORT void asio_conn_write(void* p, const char* data, size_t size, int dest_id) {
    auto conn = (connection::pointer*)p;
    (*conn)->write(std::move(string(data, size)), dest_id);
}

extern "C"
DLL_EXPORT void asio_conn_close(void* p) {
    auto conn = (connection::pointer*)p;
    (*conn)->close();
}

extern "C"
DLL_EXPORT void* asio_get_original_dst(void* p) {
    auto conn = (connection::pointer*)p;
    static sockaddr_storage rtn;
    if ((*conn)->get_original_dst(&rtn))
        return &rtn;
    else
        return NULL;
}

//----------------------

extern "C"
DLL_EXPORT void asio_sleep(int dest_id, unsigned int sec) {
    auto timer = boost::shared_ptr<asio::deadline_timer>(
        new asio::deadline_timer(io_context, boost::posix_time::seconds(sec)) );
    timer->async_wait(
        [timer, dest_id](const asio::error_code& ec)
        {
            if (!ec) {
                push_event(EVT_CONTINUE, dest_id, NULL, "");
            }else{
                push_event(EVT_CONTINUE, dest_id, NULL, ec.message());
            }
        });
}

extern "C"
DLL_EXPORT bool asio_stopped() {
    return io_context.stopped();
}

extern "C"
struct event_message_for_ffi {
    char type;
    int dest_id;
    void* source;
    const char* data;
    size_t data_len;
};

extern "C"
DLL_EXPORT event_message_for_ffi* asio_get(int wait_sec) {
    try {
        if (g_evt_queue.empty()) {
            if(io_context.stopped()){
                io_context.restart();
            }
            if (wait_sec < 0) {
                io_context.run_one();
            } else {
                io_context.run_one_for(chrono::seconds(wait_sec));
            }
        }
        if (g_evt_queue.empty()) return NULL;

        static string buff;
        static event_message_for_ffi rtn;
        auto &evt = g_evt_queue.front();
        rtn.type = evt.type;
        rtn.dest_id = evt.dest_id;
        rtn.source = evt.source;
        buff = evt.data;
        rtn.data = buff.c_str();
        rtn.data_len = buff.size();
        g_evt_queue.pop_front();
        return &rtn;
    } catch (std::exception& e) {
        std::cerr << "LuaAsio Exception: " << e.what() << "\n";
        return NULL;
    }
}

//---------------------------------------------------

