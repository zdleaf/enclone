#ifndef ENCLONE_H
#define ENCLONE_H

#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <iterator>
#include <vector>
//#include <fstream>
#include <boost/asio.hpp> // unix domain local sockets
#include <boost/program_options.hpp> // CLI arguments parsing

namespace fs = std::filesystem;
namespace asio = boost::asio;
namespace po = boost::program_options;
using boost::asio::local::stream_protocol;
using std::string;
using std::cout;
using std::endl;

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

enum { max_length = 2048 };

class enclone{
    public:
        enclone(const int& argc, char** const argv);
        ~enclone();

    private:
        asio::io_service io_service;
        
        int showOptions(const int& argc, char** const argv);

        bool sendRequest(string request);

        bool addWatch(string path, bool recursive);
        bool listLocal();
        bool listRemote();

        std::vector<string> toAdd{}; // paths to watch
        std::vector<string> toRecAdd{};   // recursive directories to watch
        std::vector<string> toDel{}; // paths to delete watches to
        


};

#else // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
# error Local sockets not available on this platform.
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

#endif