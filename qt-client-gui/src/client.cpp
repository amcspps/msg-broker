#include "client.h"
#include "utils.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <filesystem>


namespace po = boost::program_options;
namespace pt = boost::property_tree;
namespace fs = std::filesystem;


void Client::start_logging() {
  auto log_dir = fs::path(m_log_dir);
  if (!fs::exists(log_dir)) {
    fs::create_directories(log_dir);
  }

  if(m_log_lvl == "INFO") {
    google::SetLogDestination(google::GLOG_INFO, (log_dir / "INFO_").c_str());
  }
  else if (m_log_lvl == "ERROR") {
    google::SetLogDestination(google::GLOG_ERROR, (log_dir / "ERROR_").c_str());
  }
  else {
    google::SetLogDestination(google::GLOG_INFO, (log_dir / "INFO_").c_str());
    LOG(INFO) << "Unknown log lvl. Default: INFO";
  }
}

std::string Client::get_log_dir() {
  return m_log_dir;
}

void Client::set_log_dir(std::string log_dir) {
  m_log_dir = fs::absolute(log_dir).string();
}

void Client::set_log_lvl(std::string log_lvl) {
  m_log_lvl = log_lvl;
}

void Client::set_hostname(std::string hostname) {
  m_hostname = hostname;
}

void Client::set_port(int port) {
  m_port = port;
}

void Client::load_cfg() {
  try {
    pt::ptree tree;
    pt::ini_parser::read_ini(fs::absolute(m_cfg_path).string(), tree);
    set_hostname(tree.get<std::string>("client.host"));
    set_port(tree.get<int>("client.port"));
    set_log_dir(tree.get<std::string>("client.log_dir"));
    set_log_lvl(tree.get<std::string>("client.log_lvl"));
    LOG(INFO) << "Client: config parsed successfully!";
  }
  catch (std::exception& ex) {
    LOG(ERROR) << "Client: error while parsing cfg. Error message: " << ex.what();
  }
}

void Client::load_cfg(po::variables_map& vm) {
  try{
    pt::ptree tree;
    pt::ini_parser::read_ini(fs::absolute(vm["config"].as<std::string>()).string(), tree);
    set_hostname(tree.get<std::string>("client.host"));
    set_port(tree.get<int>("client.port"));
  }
  catch(std::exception& ex) {
    LOG(ERROR) << "Client: error while parsing cfg. Error message: " << ex.what();
  }
}

void Client::publish_request(int num) {
  TestTask::Messages::Request request;
  request.set_id("test-request");
  request.set_req(num);
  std::string serialized_request;
  if (!request.SerializeToString(&serialized_request)) {
    LOG(ERROR) << "Client: Failed to serialize Protobuf request";
  }

  amqp_basic_properties_t props;
  props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_REPLY_TO_FLAG;
  props.content_type = amqp_cstring_bytes("text/plain");
  props.reply_to = amqp_bytes_malloc_dup(m_reply_to_queue);

  if (props.reply_to.bytes == NULL) {
    LOG(ERROR) << "Client: Out of memory while copying queue name";
    throw std::runtime_error("Out of memory while copying queue name");
  }

  die_on_error(amqp_basic_publish(m_conn, 1, amqp_empty_bytes,
                                  amqp_cstring_bytes("rpc_queue"), 0, 0,
                                  &props, amqp_cstring_bytes(serialized_request.c_str())),
              "Publishing");
  
  amqp_bytes_free(props.reply_to);
  LOG(INFO) << "Client: Message published. Message:" << request.req();
}

void  Client::set_consumer() {
  amqp_basic_consume(m_conn, 1, m_reply_to_queue, amqp_empty_bytes, 0, 0, 0,
                       amqp_empty_table);
    die_on_amqp_error(amqp_get_rpc_reply(m_conn), "Consuming");
    LOG(INFO) << "Client: consumer set";
}

std::tuple<bool, std::string> Client::process_response() {
  for (;;) {
    amqp_rpc_reply_t res;
    amqp_envelope_t envelope;
    amqp_maybe_release_buffers(m_conn);

    timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    res = amqp_consume_message(m_conn, &envelope, &timeout, 0);
    if(res.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION && res.library_error == AMQP_STATUS_TIMEOUT) {
      LOG(INFO) << "Client: connection timeout reached";
    }

    if (AMQP_RESPONSE_NORMAL != res.reply_type) {
      LOG(ERROR) << "Client: bad message consumption AMQP_RESPONSE_(not)_NORMAL";
      return std::tuple<bool, std::string> {false, "false"};
    }

    TestTask::Messages::Response response;
    if(!response.ParseFromString(std::string((const char*)envelope.message.body.bytes, envelope.message.body.len))) {
      LOG(ERROR) << "CLIENT: response ParseFromString() failed";
    }
    else {
      LOG(INFO) << "Client: successfully got response:" << response.res();
    }
    
    
    amqp_destroy_envelope(&envelope);
    return std::tuple<bool, std::string> {true, std::to_string(response.res())};;
  }
}


void Client::connect() {
  m_conn = amqp_new_connection() /*returns NULL or 0*/;
  if (!m_conn) {
    LOG(ERROR) << "Client: Connection creation failed";
    throw std::runtime_error("Connection creation failed");
  }
  LOG(INFO) << "Client: connection created";
}


void Client::create_tcp_socket() {
  m_socket = amqp_tcp_socket_new(m_conn);
  if (!m_socket) {
    LOG(ERROR) << "Client: Socket creation failed";
    throw std::runtime_error("creating TCP socket");
  }
  LOG(INFO) << "Client: TCP socket creation success";
}

void Client::open_tcp_socket() {
  m_status = amqp_socket_open(m_socket, m_hostname.c_str(), m_port);
  if (m_status) {
      LOG(ERROR) << "Client: Error opening TCP socket";
      throw std::runtime_error("opening TCP socket");
  }
  LOG(INFO) << "Client: TCP socket opening success";
}

void Client::login() {
  die_on_amqp_error(amqp_login(m_conn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
                               "guest", "guest"),
                    "Logging in");
  LOG(INFO) << "Client: login successful";
}

void Client::open_channel() {
  amqp_channel_open(m_conn, 1);
  die_on_amqp_error(amqp_get_rpc_reply(m_conn), "Opening channel");
  LOG(INFO) << "Client: channel opened successfully";
}

void Client::create_reply_queue() {
  amqp_queue_declare_ok_t *r = amqp_queue_declare(
        m_conn, 1, amqp_empty_bytes, 0, 0, 0, 1, amqp_empty_table);
    die_on_amqp_error(amqp_get_rpc_reply(m_conn), "Declaring queue");
    m_reply_to_queue = amqp_bytes_malloc_dup(r->queue);
    if (m_reply_to_queue.bytes == NULL) {
      LOG(ERROR) << "Client: reply queue creation failed";
      throw std::runtime_error("Out of memory while copying queue name");
    }
    LOG(INFO) << "Client: reply queue declared";
}

void Client::close_channel() {
  die_on_amqp_error(amqp_channel_close(m_conn, 1, AMQP_REPLY_SUCCESS),
                    "Closing channel");
    LOG(INFO) << "Client: channel closed successfully";
}

void Client::close_connection() {
  die_on_amqp_error(amqp_connection_close(m_conn, AMQP_REPLY_SUCCESS),
                    "Closing connection");
  LOG(INFO) << "Client: connection closed successfully";
}

void Client::disconnect() {
  die_on_error(amqp_destroy_connection(m_conn), "Ending connection");
  LOG(INFO) << "Client: disconnected";
}

void Client::set_cfg_path(po::variables_map& vm) {
  m_cfg_path = fs::absolute(vm["config"].as<std::string>()).string();
}

std::string Client::get_cfg_path() {
  return m_cfg_path;
}
