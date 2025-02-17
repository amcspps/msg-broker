#include "server.hpp"
#include <iostream>
#include <boost/program_options.hpp>


auto main(int argc, char const *const *argv) -> int {
  try {
    po::options_description desc("Allowed options");
    desc.add_options()
      ("help,h", "produce help message")
      ("config,c", po::value<std::string>()->default_value("../src/cfg.ini"), "configuration file");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 1;
    }
    
    google::InitGoogleLogging(argv[0]);

    Server& s = Server::get_instance();
    s.load_cfg(vm);
    s.start_logging();
    s.run();
    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}