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

void S3::execThread(){ // not used - not running S3 in separate thread at the moment
    initAPI();
    while(*runThreads){
        cout << "S3: Calling S3 API..." << endl; cout.flush();
        callAPI();
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    closeAPI();
}

void S3::initAPI(){
    Aws::InitAPI(options);
    {   
        s3_client = Aws::MakeShared<Aws::S3::S3Client>("S3Client");
        auto executor = Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>("executor", 25);
        Aws::Transfer::TransferManagerConfiguration transferConfig(executor.get());
        transferConfig.s3Client = s3_client;
        transferManager = Aws::Transfer::TransferManager::Create(transferConfig);
        cout << "S3: Configured API..." << endl;
    }
}

void S3::closeAPI(){
    cout << "S3: Closed API..." << endl;
    Aws::ShutdownAPI(options);
}

void S3::callAPI(){
    initAPI();
    //listBuckets();
    //listObjects();
    uploadQueue();
    downloadQueue();
    //deleteQueue();
    closeAPI();
}

string S3::callAPI(string arg){
    initAPI();
    string response;
    if(arg == "listObjects"){
        response = listObjects();
    } else if(arg == "listBuckets"){
        response = listBuckets();
    }
    closeAPI();
    return response;
}

void S3::uploadQueue(){
    std::lock_guard<std::mutex> guard(mtx);
    std::pair<string, string> returnValue;
    std::pair<string, string> *returnValuePtr = &returnValue;
    while(dequeueUpload(returnValuePtr)){ // returns true until queue is empty
        uploadObject(BUCKET_NAME, returnValuePtr->first, returnValuePtr->second);
    }
    cout << "S3: uploadQueue is empty" << endl;
}

void S3::downloadQueue(){
    std::lock_guard<std::mutex> guard(mtx);
    std::pair<string, string> returnValue;
    std::pair<string, string> *returnValuePtr = &returnValue;
    while(dequeueDownload(returnValuePtr)){ // returns true until queue is empty
        downloadObject(BUCKET_NAME, returnValuePtr->first, returnValuePtr->second);
    }
    cout << "S3: downloadQueue is empty" << endl;
}

void S3::deleteQueue(){
    std::lock_guard<std::mutex> guard(mtx);
    std::string returnValue;
    std::string* returnValuePtr = &returnValue;
    while(dequeueDelete(returnValuePtr)){ // returns true until queue is empty
        Aws::String objectName(*returnValuePtr);
        bool result = deleteObject(objectName, BUCKET_NAME);
    }
    cout << "S3: deleteQueue is empty" << endl;
}

string S3::listBuckets(){
    std::ostringstream ss;
    auto outcome = s3_client->ListBuckets();
    if (outcome.IsSuccess())
    {
        ss << "S3: List of available buckets:" << std::endl;

        Aws::Vector<Aws::S3::Model::Bucket> bucket_list = outcome.GetResult().GetBuckets();

        for (auto const &bucket : bucket_list)
        {
            ss << bucket.GetName() << std::endl;
        }
    } else {
        ss << "S3: ListBuckets error: "
                  << outcome.GetError().GetExceptionName() << std::endl
                  << outcome.GetError().GetMessage() << std::endl;
    }
    cout << ss.str();
    return ss.str();
}

string S3::listObjects(){
    std::ostringstream ss;
    Aws::S3::Model::ListObjectsRequest objects_request;
    objects_request.WithBucket(BUCKET_NAME);

    auto list_objects_outcome = s3_client->ListObjects(objects_request);

    if (list_objects_outcome.IsSuccess()) {
        Aws::Vector<Aws::S3::Model::Object> object_list =
            list_objects_outcome.GetResult().GetContents();
        ss << "S3: Files on S3 bucket " << BUCKET_NAME << std::endl;
        for (auto const &s3_object : object_list)
        {
            auto modtime = s3_object.GetLastModified().ToGmtString(Aws::Utils::DateFormat::ISO_8601);
            ss << "* " << s3_object.GetKey() << " modtime: " << modtime << std::endl;
            remoteObjects.push_back(s3_object.GetKey().c_str());
        }
    } else {
        ss << "S3: ListObjects error: " <<
            list_objects_outcome.GetError().GetExceptionName() << " " <<
            list_objects_outcome.GetError().GetMessage() << std::endl;
    }
    cout << ss.str();
    return ss.str();
}

bool S3::uploadObject(const Aws::String& bucketName, const std::string& path, const std::string& objectName){
    Aws::String awsPath(path);
    Aws::String awsObjectName(objectName);

    cout << "S3: attempting to upload " << path << " as " << objectName << endl;
    auto uploadHandle = transferManager->UploadFile(awsPath, bucketName, awsObjectName, "", Aws::Map<Aws::String, Aws::String>());
    uploadHandle->WaitUntilFinished();
    if(uploadHandle->GetStatus() == Aws::Transfer::TransferStatus::COMPLETED){
        cout << "S3: Upload of " << path << " as " << objectName << " successful" << endl;
        assert(uploadHandle->GetBytesTotalSize() == uploadHandle->GetBytesTransferred()); // verify upload expected length of data
        remote->uploadSuccess(path, objectName, remoteID);
        return true;
    } else {
        cout << "S3: Upload of " << path << " as " << objectName << " failed with status: " << uploadHandle->GetStatus() << endl;
    }
    return false;
}

bool S3::downloadObject(const Aws::String& bucketName, const std::string& writeToPath, const std::string& objectName){
    // the directory we want to download to
    string downloadDir = "/home/zach/enclone/dl"; 

    // split path into directory path and filename
    std::size_t found = writeToPath.find_last_of("/");
    string dirPath = downloadDir + writeToPath.substr(0,found+1); // put 
    string fileName = writeToPath.substr(found+1);
    cout << "path is " << dirPath << ", fileName is " << fileName << endl;

    // need to create the full directory structure at path if it's doesn't already exist, or download will say successful but not save to disk
    try {
        fs::create_directories(dirPath);
        cout << "Created directories for path: " << dirPath << endl;
    }
    catch (std::exception& e) {
        std::cout << e.what() << std::endl;
    }

    Aws::String awsWriteToFile(dirPath + fileName);
    Aws::String awsObjectName(objectName);

    auto downloadHandle = transferManager->DownloadFile(bucketName, awsObjectName, awsWriteToFile);
    downloadHandle->WaitUntilFinished();
    if(downloadHandle->GetStatus() == Aws::Transfer::TransferStatus::COMPLETED){
        if (downloadHandle->GetBytesTotalSize() == downloadHandle->GetBytesTransferred()) {
            cout << "S3: Download of " << objectName << " to " << awsWriteToFile << " successful" << endl;
            return true;
        } else {
            cout << "S3: Bytes downloaded did not equal requested number of bytes: " << downloadHandle->GetBytesTotalSize() << downloadHandle->GetBytesTransferred() << std::endl;
        }
    } else {
        cout << "S3: Download of " << objectName << " to " << awsWriteToFile << " failed with message: " << downloadHandle->GetStatus() << endl;
    }

    size_t retries = 0; // retry download if failed (up to 5 times)
    while (downloadHandle->GetStatus() == Aws::Transfer::TransferStatus::FAILED && retries++ < 5)
    {
        std::cout << "S3: Retrying download again. Attempt " << retries << " of 5" << std::endl;
        transferManager->RetryDownload(downloadHandle);
        downloadHandle->WaitUntilFinished();
    }

    std::cout << "S3: Download of " << objectName << " to " << awsWriteToFile << " failed after maximum retry attempts" << std::endl;
    return false;
}

// legacy upload/delete/download below - not using TransferManager

bool S3::put_s3_object( const Aws::String& s3_bucketName, 
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
    auto put_object_outcome = s3_client->PutObject(object_request);
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

bool S3::deleteObject(const Aws::String& objectName, const Aws::String& fromBucket){
    Aws::S3::Model::DeleteObjectRequest request;

    request.WithKey(objectName).WithBucket(fromBucket);

    Aws::S3::Model::DeleteObjectOutcome result = s3_client->DeleteObject(request);

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

bool S3::get_s3_object(const Aws::String& objectName, const Aws::String& fromBucket)
{
    // s3_client.getObject(new GetObjectRequest(bucket,key),file)

    Aws::S3::Model::GetObjectRequest objectRequest;
    objectRequest.SetBucket(fromBucket);
    objectRequest.SetKey(objectName);

    Aws::S3::Model::GetObjectOutcome getObjectOutcome = s3_client->GetObject(objectRequest);

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