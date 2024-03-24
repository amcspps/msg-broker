#include "server.hpp"
#include "../proto-files/message.pb.h"
#include <stdexcept>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/filesystem.hpp>
#include <glog/logging.h>

namespace pt = boost::property_tree;
namespace fs = boost::filesystem;


std::string Server::get_log_level() {
  return m_log_level;
};
void Server::set_log_level(std::string& lvl) {
  m_log_level = lvl;
};


std::string Server::get_hostname() {
  return m_hostname;
};

int Server::get_port() {
  return m_port;
};

std::string Server::get_queuename() {
  return m_queuename;
};


void Server::set_hostname(std::string hostname) {
  m_hostname = hostname;
};

void Server::set_port(int port) {
  m_port = port;
};

void Server::set_queuename(std::string queuename) {
  m_queuename = queuename;
};

void Server::load_cfg(po::variables_map& vm) {
  try {
    pt::ptree tree;
    pt::ini_parser::read_ini(fs::absolute(vm["config"].as<std::string>()).string(), tree); 
    set_hostname(tree.get<std::string>("server.host"));
    set_port(tree.get<int>("server.port"));
    set_queuename(tree.get<std::string>("server.queuename"));
    
    LOG(INFO) << "Server: config parsed suceessfully!";
  }
  catch(std::exception& ex) {
    LOG(ERROR) << "Server: error while parsing cfg. Erorr message:" <<
               ex.what();
  }
}

void Server::connect() {
  m_conn = amqp_new_connection() /*returns NULL or 0*/;
  if (!m_conn) {
    LOG(ERROR) << "Server: Connection creation failed";
    throw std::runtime_error("Connection creation failed");
  }
  LOG(INFO) << "Server: connection created";
};

void Server::create_tcp_socket() {
  m_socket = amqp_tcp_socket_new(m_conn);
  if (!m_socket) {
    LOG(ERROR) << "Server: Socket creation failed";
    throw std::runtime_error("Error creating TCP socket");
  }
  LOG(INFO) << "Server: TCP socket created";
};

void Server::open_tcp_socket() {
  m_status = amqp_socket_open(m_socket, m_hostname.c_str(), m_port);
  if (m_status) {
    LOG(ERROR) << "Server: Error opening TCP socket";
    throw std::runtime_error("Error opening TCP socket");
  }
  LOG(INFO) << "Server: TCP socket opened";
};

void Server::login() {
  die_on_amqp_error(amqp_login(m_conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
                               "guest", "guest"),
                    "Logging in");
  LOG(INFO) << "Server: login successful";                    
};

void Server::open_channel() {
  amqp_channel_open(m_conn, 1);
  die_on_amqp_error(amqp_get_rpc_reply(m_conn), "Opening channel");
  LOG(INFO) << "Server: channel opened successfully";
};

void Server::declare_queue() {
    amqp_queue_declare_ok_t *r = amqp_queue_declare(
        m_conn, 1, amqp_cstring_bytes(m_queuename.c_str()), 0, 0, 0, 1, amqp_empty_table);
    die_on_amqp_error(amqp_get_rpc_reply(m_conn), "Declaring queue");
    LOG(INFO) << "Server: queue rpc_queue declared";
};

void Server::set_queue_listener() {
  amqp_basic_consume(m_conn, 1, amqp_cstring_bytes(m_queuename.c_str()), amqp_empty_bytes,
                       0, 0, 0, amqp_empty_table);
  die_on_amqp_error(amqp_get_rpc_reply(m_conn), "Consuming");
  LOG(INFO) << "Server: queue listener set";
};

void Server::close_channel() {
  die_on_amqp_error(amqp_channel_close(m_conn, 1, AMQP_REPLY_SUCCESS),
                    "Closing channel");
  LOG(INFO) << "Server: channel closed successfully";                  
};

void Server::close_connection() {
  die_on_amqp_error(amqp_connection_close(m_conn, AMQP_REPLY_SUCCESS),
                    "Closing connection");
  LOG(INFO) << "Server: connection closed successfully";                    
};

void Server::disconnect() {
  die_on_error(amqp_destroy_connection(m_conn), "Ending connection");
  LOG(INFO) << "Server: disconnected";
};

void Server::process() {
  for (;;) {
    amqp_rpc_reply_t res;
    amqp_envelope_t envelope;
    amqp_maybe_release_buffers(m_conn);

    timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    res = amqp_consume_message(m_conn, &envelope, &timeout, 0);
    if(res.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION && res.library_error == AMQP_STATUS_TIMEOUT) {
      LOG(INFO) << "Server: timeout reached. Exiting...";
    } 
    if (AMQP_RESPONSE_NORMAL != res.reply_type) {
      LOG(ERROR) << "Server: bad message consumption AMQP_RESPONSE_(not)_NORMAL";
      break;
    }

    TestTask::Messages::Request request;
    request.ParseFromString(std::string((const char*)envelope.message.body.bytes));
    //std::cout << "received from client: " << request.req() << std::endl;
    
    LOG(INFO) << "Server: received from client:" << request.req();
    
    amqp_basic_properties_t reply_props;
    reply_props._flags = AMQP_BASIC_CORRELATION_ID_FLAG;
    reply_props.correlation_id = envelope.message.properties.correlation_id;
    

    TestTask::Messages::Response response;
    response.set_id("test-response");
    response.set_res(request.req()*2); /* response = request * 2*/
    std::string serialized_response;
    if (!response.SerializeToString(&serialized_response)) {
      LOG(ERROR) << "Server: response SerializeToString() failed";
    }
    die_on_error(amqp_basic_publish(m_conn, 1, amqp_empty_bytes,
                                    envelope.message.properties.reply_to, 0, 0,
                                    &reply_props, amqp_cstring_bytes(serialized_response.c_str())),
                 "Publishing");
    LOG(INFO) << "Server: successfully published: " << response.res();
    amqp_destroy_envelope(&envelope);

  }
}

void Server::run() {
  connect();
  create_tcp_socket();
  open_tcp_socket();
  login();
  open_channel();
  declare_queue();
  set_queue_listener();
  process();
  close_channel();
  close_connection();
  disconnect();
}
