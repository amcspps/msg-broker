#include "mainwindow.h"
#include "client.h"
#include <QApplication>
#include <boost/filesystem.hpp>

#include <iostream>

std::string logdir = "";

int main(int argc, char *argv[]) {
  try {
    po::options_description desc("Allowed options");
    desc.add_options()
      ("help,h", "produce help message")
      ("config,c", po::value<std::string>()->default_value("../src/cfg.ini"), "configuration file")
      ("log,l", po::value<std::string>()->default_value("../log"), "log file");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 1;
    }
    google::InitGoogleLogging(argv[0]);

    logdir = boost::filesystem::absolute(vm["log"].as<std::string>()).string();

    LOG(INFO) << "Arguments parsed successfully";

    Client &cl = Client::get_instance();
    cl.set_cfg_path(vm);
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
  }
  catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
  return 1;
  }
}
