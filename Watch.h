#ifndef WATCH_H
#define WATCH_H

#include <iostream>
#include <filesystem>
#include <string>
//#include <boost/filesystem.hpp>

//namespace b = boost::filesystem;
namespace fs = std::filesystem;
using namespace std;

// c++17 std::filesystem
// linux filesystem changes - inotify

class Watch {
    private:
        string path;

    public:
        Watch(string path);
        void listDir();

};

#endif