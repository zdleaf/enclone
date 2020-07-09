#ifndef S3_H
#define S3_H

#include <string>
#include <filesystem>
#include <fstream>
#include <memory>

// concurrency/multi-threading
#include <thread>
#include <mutex>
#include <atomic>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/Bucket.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/Object.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>

#include <aws/core/utils/threading/Executor.h>
#include <aws/transfer/TransferManager.h>
#include <aws/core/utils/memory/AWSMemory.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/core/utils/stream/PreallocatedStreamBuf.h>
#include <aws/core/utils/StringUtils.h>
#include <fstream>

#include <enclone/remote/Queue.h>
#include <enclone/remote/Remote.h>

namespace fs = std::filesystem;
using std::string;
using std::cout;
using std::endl;

class Remote;

class S3: public Queue {
    private:
        int remoteID = 1; // each remote has a unique remoteID

        Remote *remote; // ptr to class instance of Remote that spawned this S3 instance
        
        std::vector<string> remoteObjects;

        Aws::SDKOptions options;
        const Aws::String BUCKET_NAME = "enclone";
        
        // concurrency/multi-threading
        std::mutex mtx;
        std::atomic_bool *runThreads; // ptr to flag indicating if execThread should loop or close down

        // S3 specific
        void uploadQueue(std::shared_ptr<Aws::Transfer::TransferManager> transferManager);
        void deleteQueue(Aws::S3::S3Client s3_client);

        bool listBuckets(std::shared_ptr<Aws::S3::S3Client> s3_client);
        bool listObjects(std::shared_ptr<Aws::S3::S3Client> s3_client);

        bool uploadObject(std::shared_ptr<Aws::Transfer::TransferManager> transferManager, const Aws::String& bucketName, const std::string& path, const std::string& objectName);

        bool put_s3_object(std::shared_ptr<Aws::S3::S3Client> s3_client, const Aws::String& s3_bucket_name, const std::string& path, const Aws::String& s3_object_name);
        bool delete_s3_object(Aws::S3::S3Client s3_client, const Aws::String& objectName, const Aws::String& fromBucket);
        bool get_s3_object(Aws::S3::S3Client s3_client, const Aws::String& objectName, const Aws::String& fromBucket);

    public:
        S3(std::atomic_bool *runThreads, Remote *remote);
        ~S3();

        //void setPtr(std::shared_ptr<Remote> remote);

        // delete copy constructors - this class should not be copied
        S3(const S3&) = delete;
        S3& operator=(const S3&) = delete;

        void callAPI();
        void execThread();
};

#endif