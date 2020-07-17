#include <enclone/remote/Remote.h>

Remote::Remote(std::atomic_bool *runThreads)
{
    this->runThreads = runThreads;
    s3 = std::make_shared<S3>(runThreads, this);
}

void Remote::setPtr(std::shared_ptr<Watch> watch){
    this->watch = watch;
}

void Remote::execThread(){
    while(*runThreads){
        std::this_thread::sleep_for(std::chrono::seconds(10));
        //cout << "Remote: Calling Remote cloud storage..." << endl; cout.flush();
        callRemotes();
    }
}

void Remote::callRemotes(){
    std::lock_guard<std::mutex> guard(mtx);
    s3->callAPI("transfer"); // change to transfer
}

bool Remote::queueForUpload(std::string path, std::string objectName, std::time_t modtime){
    std::lock_guard<std::mutex> guard(mtx);
    // call remotes
    return s3->enqueueUpload(path, objectName, modtime);
}

bool Remote::queueForDownload(std::string path, std::string objectName){
    std::lock_guard<std::mutex> guard(mtx);
    // call remotes
    return s3->enqueueDownload(path, objectName);
}

bool Remote::queueForDelete(std::string objectName){
    std::lock_guard<std::mutex> guard(mtx);
    // call remotes
    return s3->enqueueDelete(objectName);
}

void Remote::uploadSuccess(std::string path, std::string objectName, int remoteID){ // update fileIndex if upload to remote is succesfull
    watch->uploadSuccess(path, objectName, remoteID);
}

string Remote::listObjects(){
    mtx.lock();
    std::vector<string> objects;
    try {
        objects = s3->getObjects();
    } catch(const std::exception& e){ // listObjects may fail due invalid credentials etc
        return e.what();
    }
    mtx.unlock();
    std::ostringstream ss;
    for(string pathHash: objects){
        auto pair = watch->resolvePathHash(pathHash);
        ss << pair.first << ":" << watch->displayTime(pair.second) << endl;
    }
    return ss.str();
}
