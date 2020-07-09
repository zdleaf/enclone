#include <enclone/remote/S3.h>

// example API calls - https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/examples-s3-objects.html

S3::S3(std::atomic_bool *runThreads, Remote *remote)
{
    this->remote = remote;
    this->runThreads = runThreads;       

    // S3 logging options
    options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info; // turn logging on using the default logger
    options.loggingOptions.defaultLogPrefix = "log/aws_sdk_";
}

S3::~S3()
{

}

void S3::execThread(){
    while(*runThreads){
        cout << "S3: Calling S3 API..." << endl; cout.flush();
        callAPI();
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void S3::callAPI(){
    Aws::InitAPI(options);
    {
        Aws::S3::S3Client s3_client;
        //listBuckets(s3_client);
        listObjects(s3_client);
        uploadQueue(s3_client);
        deleteQueue(s3_client);
        get_s3_object(s3_client, "1b8716cc70e1874aa0a1a4c485ec991fb06b3b694deff33b1acd1f036cde2fe3", BUCKET_NAME);
    }
    Aws::ShutdownAPI(options);
}

void S3::uploadQueue(Aws::S3::S3Client s3_client){
    std::lock_guard<std::mutex> guard(mtx);
    std::pair<string, string> returnValue;
    std::pair<string, string> *returnValuePtr = &returnValue;
    while(dequeueUpload(returnValuePtr)){ // returns true until queue is empty
        std::string path(returnValuePtr->first);
        Aws::String objectName(returnValuePtr->second);
        bool success = put_s3_object(s3_client, BUCKET_NAME, path, objectName);
        if(success){
            remote->uploadSuccess(path, objectName.c_str(), remoteID);
        }
    }
    cout << "S3: uploadQueue is empty" << endl;
}

void S3::deleteQueue(Aws::S3::S3Client s3_client){
    std::lock_guard<std::mutex> guard(mtx);
    std::string returnValue;
    std::string* returnValuePtr = &returnValue;
    while(dequeueDelete(returnValuePtr)){ // returns true until queue is empty
        Aws::String objectName(*returnValuePtr);
        bool result = delete_s3_object(s3_client, objectName, BUCKET_NAME);
    }
    cout << "S3: deleteQueue is empty" << endl;
}

bool S3::listBuckets(Aws::S3::S3Client s3_client){
    auto outcome = s3_client.ListBuckets();
    if (outcome.IsSuccess())
    {
        std::cout << "S3: List of available buckets:" << std::endl;

        Aws::Vector<Aws::S3::Model::Bucket> bucket_list = outcome.GetResult().GetBuckets();

        for (auto const &bucket : bucket_list)
        {
            std::cout << bucket.GetName() << std::endl;
        }

        std::cout << std::endl;
        return true;
    } else {
        std::cout << "S3: ListBuckets error: "
                  << outcome.GetError().GetExceptionName() << std::endl
                  << outcome.GetError().GetMessage() << std::endl;
        return false;
    }
}

bool S3::listObjects(Aws::S3::S3Client s3_client){
    Aws::S3::Model::ListObjectsRequest objects_request;
    objects_request.WithBucket(BUCKET_NAME);

    auto list_objects_outcome = s3_client.ListObjects(objects_request);

    if (list_objects_outcome.IsSuccess()) {
        Aws::Vector<Aws::S3::Model::Object> object_list =
            list_objects_outcome.GetResult().GetContents();
        std::cout << "S3: Files on S3 bucket " << BUCKET_NAME << std::endl;
        for (auto const &s3_object : object_list)
        {
            auto modtime = s3_object.GetLastModified().ToGmtString(Aws::Utils::DateFormat::ISO_8601);
            std::cout << "* " << s3_object.GetKey() << " modtime: " << modtime << std::endl;
            remoteObjects.push_back(s3_object.GetKey().c_str());
        }
        return true;
    } else {
        std::cout << "S3: ListObjects error: " <<
            list_objects_outcome.GetError().GetExceptionName() << " " <<
            list_objects_outcome.GetError().GetMessage() << std::endl;
        return false;
    }
}

bool S3::put_s3_object( Aws::S3::S3Client s3_client,
                        const Aws::String& s3_bucketName, 
                        const std::string& path,
                        const Aws::String& s3_objectName)
{
    // check again that path exists
    if (!fs::exists(path)) {
        std::cout << "S3: ERROR: NoSuchFile: The specified file does not exist" << std::endl;
        return false;
    }

    Aws::S3::Model::PutObjectRequest object_request;

    object_request.SetBucket(s3_bucketName);
    object_request.SetKey(s3_objectName);
    const std::shared_ptr<Aws::IOStream> input_data = 
        Aws::MakeShared<Aws::FStream>("SampleAllocationTag", 
                                      path.c_str(), 
                                      std::ios_base::in | std::ios_base::binary);
    object_request.SetBody(input_data);

    // Put the object
    auto put_object_outcome = s3_client.PutObject(object_request);
    if (!put_object_outcome.IsSuccess()) {
        auto error = put_object_outcome.GetError();
        std::cout << "S3: ERROR: " << error.GetExceptionName() << ": " 
            << error.GetMessage() << std::endl;
        cout << "S3: Upload of " << path << " as " << s3_objectName << " failed" << endl;
        return false;
    }
    cout << "S3: Upload of " << path << " as " << s3_objectName << " successful" << endl; 
    return true;
}

bool S3::delete_s3_object(Aws::S3::S3Client s3_client, const Aws::String& objectName, const Aws::String& fromBucket){
    Aws::S3::Model::DeleteObjectRequest request;

    request.WithKey(objectName).WithBucket(fromBucket);

    Aws::S3::Model::DeleteObjectOutcome result = s3_client.DeleteObject(request);

    if (!result.IsSuccess())
    {
        cout << "S3: Delete of " << objectName << " failed" << endl;
        auto err = result.GetError();
        std::cout << "Error: DeleteObject: " <<
            err.GetExceptionName() << ": " << err.GetMessage() << std::endl;
        return false;
    }
    cout << "S3: Delete of " << objectName << " successful" << endl;
    return true;
}

bool S3::get_s3_object(Aws::S3::S3Client s3_client, const Aws::String& objectName, const Aws::String& fromBucket)
{
    // s3_client.getObject(new GetObjectRequest(bucket,key),file)

    Aws::S3::Model::GetObjectRequest objectRequest;
    objectRequest.SetBucket(fromBucket);
    objectRequest.SetKey(objectName);

    Aws::S3::Model::GetObjectOutcome getObjectOutcome = s3_client.GetObject(objectRequest);

    if (getObjectOutcome.IsSuccess())
    {
        auto& retrieved_file = getObjectOutcome.GetResultWithOwnership().GetBody();

        // Print a beginning portion of the text file.
        std::cout << "Beginning of file contents:\n";
        char file_data[255] = { 0 };
        retrieved_file.getline(file_data, 254);
        std::cout << file_data << std::endl;

        return true;
    }
    else
    {
        auto err = getObjectOutcome.GetError();
        std::cout << "Error: GetObject: " <<
            err.GetExceptionName() << ": " << err.GetMessage() << std::endl;

        return false;
    }
}