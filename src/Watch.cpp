#include <enclone/Watch.h>

Watch::Watch(std::shared_ptr<DB> db, std::atomic_bool *runThreads){ // constructor
    this->db = db; // set DB handle
    this->runThreads = runThreads;
}

Watch::~Watch(){ // destructor
    execQueuedSQL(); // execute any pending SQL before closing
}

void Watch::setPtr(std::shared_ptr<Remote> remote){
    this->remote = remote;
}

void Watch::execThread(){
    restoreDB();

    //addWatch("/home/zach/enclone/notexist", true); // check non existent file
    addWatch("/home/zach/enclone/tmp2", true); // recursive add
    //addWatch("/home/zach/enclone/tmp/subdir", false); // check dir already added
    //addWatch("/home/zach/enclone/tmp/file1", false); // check file already added

    for(auto elem: fileIndex){
        remote->queueForDownload(elem.first, elem.second.back().pathhash);
    }

    while(*runThreads){
        cout << "Watch: Scanning for file changes..." << endl; cout.flush(); 
        scanFileChange();
        execQueuedSQL();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

std::unordered_map<string, std::vector<FileVersion>>* Watch::getFileIndex(){
    return &fileIndex;
}

string Watch::addWatch(string path, bool recursive){
    std::lock_guard<std::mutex> guard(mtx);
    std::stringstream response;
    fs::file_status s = fs::status(path);
    if(!fs::exists(s)){                 // file/directory does not exist
        response << "Watch: " << path << " does not exist";
    } else if(fs::is_directory(s)){     // adding a directory to watch
        return addDirWatch(path, recursive);
    } else if(fs::is_regular_file(s)){  // adding a regular file to watch
        return addFileWatch(path);
    } else {                            // any other file type, e.g. IPC pipe
        response << "Watch: " << path << " does not exist";
    }
    std::cout << response.str() << endl;
    return response.str();
}

string Watch::addDirWatch(string path, bool recursive){
    auto result = dirIndex.insert({path, recursive});
    std::stringstream response;
    if(result.second){ // check if insertion was successful i.e. result.second = true (false when already exists in map)
        response << "Watch: "<< "Added watch to directory: " << path;
        sqlQueue << "INSERT or IGNORE INTO dirIndex (PATH, RECURSIVE) VALUES ('" << path << "'," << (recursive ? "TRUE" : "FALSE") << ");"; // queue SQL insert
        for (const auto & entry : fs::directory_iterator(path)){ // iterate through all directory entries
            fs::file_status s = fs::status(entry);
            if(fs::is_directory(s) && recursive) { // check recursive flag before adding directories recursively
                //cout << "Recursively adding: " << entry.path() << endl;
                response << addWatch(entry.path().string(), true); 
            } else if(fs::is_regular_file(s)){
                response << addFileWatch(entry.path().string());
            } else if(!fs::is_directory(s)){ 
                cout << "Watch: " << "Unknown file encountered: " << entry.path().string() << endl; 
            }
        }
    } else { // duplicate - directory was not added
        response << "Watch: " << "Watch to directory already exists: " << path << endl;
    }
    std::cout << response.str() << endl;
    return response.str();
}

string Watch::addFileWatch(string path){
    
    // temporary extension exclusions - ignore .swp files
    if(fs::path(path).extension() == ".swp"){
        return "ignored .swp file";
    }

    auto result = fileIndex.insert({path, std::vector<FileVersion>()});
    std::stringstream response;

    if(result.second){ // check if insertion was successful i.e. result.second = true (false when already exists in map)
        cout << "Watch: " << "Added watch to file: " << path << endl;
        addFileVersion(path);
    } else { // duplicate
        cout << "Watch: " << "Watch to file already exists: " << path << endl;
    }

    return response.str();
}

void Watch::addFileVersion(std::string path){
    auto fileVector = &fileIndex[path];
    auto fstime = fs::last_write_time(path); // get modtime from file
    std::time_t modtime = decltype(fstime)::clock::to_time_t(fstime);
    string pathHash = Encryption::hashPath(path); // compute unique filename hash for file
    fileVector->push_back(FileVersion{modtime, pathHash}); // create new FileVersion struct object and push to back of vector
    cout << "Watch: " << "Added file version: " << path << " with hash: " << pathHash.substr(0,10) << "..." << " modtime: " << modtime << endl;

    // queue for upload on remote and insertion into DB
    sqlQueue << "INSERT or IGNORE INTO fileIndex (PATH, MODTIME, PATHHASH, LOCALEXISTS) VALUES ('" << path << "'," << modtime << ",'" << pathHash << "',TRUE" << ");"; // if successful, queue an SQL insert into DB
    remote->queueForUpload(path, pathHash); 
}

void Watch::scanFileChange(){
    // existing files that are being watched
    for(auto elem: fileIndex){
        string path = elem.first;

        // do not scan for file changes if file is already marked as not existing locally
        if(elem.second.back().localExists == false){ break; } 

        // if file has been deleted, but is still marked as existing locally
        if(!fs::exists(path) && elem.second.back().localExists == true){ 
            std::lock_guard<std::mutex> guard(mtx);
            cout << "Watch: " << "File no longer exists: " << path << endl;
            
            fileIndex[path].back().localExists = false;
            
            // delete from fileIndex if ALL versions do not exist remotely - if file doesn't exist locally or remotely in any form, it is lost
/*             if(ALL VERSIONS !remoteExist){
                fileIndex.erase(path);
                sqlQueue << "DELETE from fileIndex where PATH='" << path << "';"; // queue deletion from DB
            } */

            // FIX ME - when to delete from remote - if sync mode is set, otherwise no
            //remote->queueForDelete(pathHashIndex[path]);
            

            break;
        }

        // if current last_write_time of file != last saved value, file has changed
        auto recentfsTime = fs::last_write_time(path);
        std::time_t recentModTime = decltype(recentfsTime)::clock::to_time_t(recentfsTime);
        //cout << "Watch: Comparing current modtime: " << recentModTime << " to saved: " << getLastModTime(path) << " file: " << path << endl;
        if(recentModTime != getLastModTime(path)){ 
            std::lock_guard<std::mutex> guard(mtx);
            cout << "Watch: " << "File change detected: " << path << endl;
            fileIndex[path].back().localExists = false;
            addFileVersion(path);
        }
    }

    // check watched directories for new files and directories
    for(auto elem: dirIndex){
        if(!fs::exists(elem.first)){ // if directory has been deleted
            std::lock_guard<std::mutex> guard(mtx);
            cout << "Watch: " << "Directory no longer exists: " << elem.first << endl;
            dirIndex.erase(elem.first); // remove watch to directory
            sqlQueue << "DELETE from dirIndex where PATH='" << elem.first << "';"; // queue deletion from DB
            break;
        }
        for (const auto &entry : fs::directory_iterator(elem.first)){ // iterate through all directory entries
            fs::file_status s = fs::status(entry);
            if(fs::is_directory(s) && elem.second) {// check recursive flag (elem.second) is true before checking if watch to dir already exists
                if(!dirIndex.count(entry.path())){   // check if directory already exists in watched map
                    std::lock_guard<std::mutex> guard(mtx);
                    cout << "Watch: " << "New directory found: " << elem.first << endl;
                    addDirWatch(entry.path().string(), true);  // add new directory and any files contained within
                }
            } else if(fs::is_regular_file(s)){
                if(!fileIndex.count(entry.path())){  // check if each file already exists
                    std::lock_guard<std::mutex> guard(mtx);
                    cout << "Watch: " << "New file found: " << entry.path() << endl;
                    addFileWatch(entry.path().string());
                }
            }
        }
    }
}

std::time_t Watch::getLastModTime(std::string path){
    return fileIndex[path].back().modtime;
}

void Watch::execQueuedSQL(){
    std::lock_guard<std::mutex> guard(mtx);
    if(sqlQueue.rdbuf()->in_avail() != 0) { // if queue is not empty
        db->execSQL(sqlQueue.str().c_str()); // convert to c style string and execute bucket
        sqlQueue.str(""); // empty bucket
        sqlQueue.clear(); // clear error codes
    }
}

void Watch::displayWatchDirs(){
    cout << "Watched directories: " << endl;
    for(auto elem: dirIndex){
        cout << elem.first << " recursive: " << elem.second << endl;
    }
}

void Watch::displayWatchFiles(){
    cout << "Watched files: " << endl;
    for(auto elem: fileIndex){
        cout << elem.first << " last modtime: " << elem.second.back().modtime << ", # of versions: " << elem.second.size() << ", exists locally: " << elem.second.back().localExists << ", exists remotely: " << elem.second.back().remoteExists << endl;
    }
}

string Watch::displayTime(std::time_t modtime) const{
    string time = std::asctime(std::localtime(&modtime));
    time.pop_back(); // asctime returns a '\n' on the end of the string - use str.pop_back to remove this
    return time; 
}

void Watch::listDir(string path){
    for (const auto & entry : fs::directory_iterator(path)){
        cout << entry.path();
        fileAttributes(entry);
    }
}

void Watch::fileAttributes(const fs::path& path){
    fs::file_status s = fs::status(path);
    // alternative: switch(s.type()) { case fs::file_type::regular: ...}
    if(fs::is_regular_file(s)) std::cout << " is a regular file\n";
    if(fs::is_directory(s)) std::cout << " is a directory\n";
    if(fs::is_block_file(s)) std::cout << " is a block device\n";
    if(fs::is_character_file(s)) std::cout << " is a character device\n";
    if(fs::is_fifo(s)) std::cout << " is a named IPC pipe\n";
    if(fs::is_socket(s)) std::cout << " is a named IPC socket\n";
    if(fs::is_symlink(s)) std::cout << " is a symlink\n";
    if(!fs::exists(s)) std::cout << " does not exist\n";
    if(fs::is_empty(path)){ std::cout << "empty\n"; } else { std::cout << "not empty\n"; };

    // size
    try {
        std::cout << "size: " << fs::file_size(path) << endl; // attempt to get size of a file
    } catch(fs::filesystem_error& e) { // e.g. is a directory, no size
        std::cout << e.what() << '\n';
    }

    // last modification time
    //std::cout << "File write time is " << displayTime(fs::last_write_time(path)) << endl;
}

void Watch::restoreDB(){
    std::lock_guard<std::mutex> guard(mtx);
    cout << "Restoring file index from DB..." << endl;
    restoreFileIdx();
    displayWatchFiles();
    cout << "Restoring directory index from DB..." << endl;
    restoreDirIdx();
    displayWatchDirs();
}

void Watch::restoreFileIdx(){
    const char getFiles[] = "SELECT * FROM fileIndex;";

    int rc, i, ncols;
    sqlite3_stmt *stmt;
    const char *tail;
    rc = sqlite3_prepare(db->getDbPtr(), getFiles, strlen(getFiles), &stmt, &tail);
    if(rc != SQLITE_OK) {
        fprintf(stderr, "restoreFileIdx: SQL error: %s\n", sqlite3_errmsg(db->getDbPtr()));
    }

    rc = sqlite3_step(stmt);
    ncols = sqlite3_column_count(stmt);

    while(rc == SQLITE_ROW) {
        string path = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        std::time_t modtime = (time_t)sqlite3_column_int(stmt, 1);
        string pathHash = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        bool localExists = sqlite3_column_int(stmt, 4);
        bool remoteExists = sqlite3_column_int(stmt, 5);
        if(fileIndex.find(path) == fileIndex.end()){ // if entry for path does not exist
            //cout << path << " does not exist in fileIndex - adding and init vector.." << endl;
            fileIndex.insert({  path, std::vector<FileVersion>{ // create entry and initialise vector 
                                    FileVersion{    modtime,  // brace initialisation of first object
                                                    pathHash, 
                                                    "",
                                                    localExists,
                                                    remoteExists  }
                                    }
                            }); 
        } else {
            //cout << path << " exists, attempting to push to vector.." << endl;
            auto fileVector = &fileIndex[path]; // get a pointer to the vector associated to the path
            fileVector->push_back(FileVersion{modtime, pathHash, ""}); // push a FileVersion struct to the back of the vector
            // this should retain the correct ordering in the vector of oldest = first in vector, most recent = last in vector
        }
        rc = sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
}

void Watch::restoreDirIdx(){
    const char getDirs[] = "SELECT * FROM dirIndex;";

    int rc, i, ncols;
    sqlite3_stmt *stmt;
    const char *tail;
    rc = sqlite3_prepare(db->getDbPtr(), getDirs, strlen(getDirs), &stmt, &tail);
    if(rc != SQLITE_OK) {
        fprintf(stderr, "restoreDirIdx: SQL error: %s\n", sqlite3_errmsg(db->getDbPtr()));
    }

    rc = sqlite3_step(stmt);
    ncols = sqlite3_column_count(stmt);

    while(rc == SQLITE_ROW) {
        string path = string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        int recursiveFlag = sqlite3_column_int(stmt, 1);
        dirIndex.insert({path, recursiveFlag});      // insert path and recursive flag into dirIndex
        rc = sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
}

void Watch::uploadSuccess(std::string path, std::string objectName, int remoteID){
    auto fileVersionVector = fileIndex.at(path);
    // set the remoteExists flag for correct entry in fileIndex
    for(auto it = fileVersionVector.rbegin(); it != fileVersionVector.rend(); ++it){ // iterate in reverse, most likely the last entry is the one we're looking for
        if(it->pathhash == objectName){ 
            it->remoteExists = true;
            // also add remoteID to list of remotes it's been uploaded to e.g. remoteLocation
        }
    }
    sqlQueue << "UPDATE fileIndex SET REMOTEEXISTS = TRUE WHERE PATHHASH ='" << objectName << "';"; // queue SQL update  
}