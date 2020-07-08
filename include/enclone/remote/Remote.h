#ifndef REMOTE_H
#define REMOTE_H

#include <iostream>
#include <string>
#include <memory>

#include <enclone/remote/S3.h>
#include <enclone/enclone.h>

// stub class - this will be the middle man for handling multiple remotes at one time, e.g. multiple upload to different locations
// needs to store which remotes are available and turned on for upload

class S3;
class enclone;
class Watch;

class Remote{
    private:
        // object pointers
        std::shared_ptr<enclone> encloneInstance;
        std::shared_ptr<Watch> watch; 

        // available remotes
        std::shared_ptr<S3> s3;

        // concurrency/multi-threading
        std::mutex mtx;
        std::atomic_bool *runThreads; // ptr to flag indicating if execThread should loop or close down

    public:
        Remote(std::atomic_bool *runThreads, std::shared_ptr<enclone> encloneInstance);

        void execThread();
        void callRemotes();

        void uploadSuccess(std::string path, std::string objectName, int remoteID); // update fileIndex if upload to remote is succesfull, returns the remoteID it was succesfully uploaded to

        bool queueForUpload(std::string path, std::string objectName);
        bool queueForDelete(std::string objectName);
};

#endif