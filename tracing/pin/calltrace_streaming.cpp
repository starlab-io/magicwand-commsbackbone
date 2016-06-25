#include <fstream>
#include <iomanip>
#include <sstream>
#include "pin.H"

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage() {
    PIN_ERROR("This Pintool writes a trace of function entrance and exit\nwith their address, timestamp, and internal threadid\n    in a streaming fashion to multiple files, one per process id\n" + 
            KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "/root/output/trace", "specify output file name");

//==============================================================
//  Analysis Routines
//==============================================================
OS_PROCESS_ID pid;

// Holds instruction count for a single procedure
// Lookup for later function information
typedef struct RtnCount {
    string _name;
    string _image;
    ADDRINT _address;
    struct RtnCount * _next;
} RTN_COUNT;

// Linked list of memory address and names for each instrumented routine
RTN_COUNT * RtnList = 0;

// pad to make sure each object is in its own data cache line  
//int pointer_size = sizeof(void*);
//int PADSIZE = 64 - pointer_size;
#if __x86_64__
    #define PADSIZE 56 // 64 - 8
#else
    #define PADSIZE 60 // 64 - 4
#endif
typedef struct DataCache {
    ofstream * outFile;
    UINT8 _pad[PADSIZE];
} DATA_CACHE;

// key for accessing TLS storage in the threads
static TLS_KEY tls_key; 

// Function that determines which child processes to follow
BOOL FollowChild(CHILD_PROCESS childProcess, VOID * userData) {
    return TRUE;
} 

ofstream* setGetOutFile(THREADID threadid, pid_t pid) {
    // set new output file and get it
    DATA_CACHE * data = (DATA_CACHE*) PIN_GetThreadData(tls_key, threadid);
    if (data) {
        data->outFile->close();
        delete data->outFile;
    } else {
        data = new DATA_CACHE;
        PIN_SetThreadData(tls_key, data, threadid);
    }
    data->outFile = new ofstream();
    ofstream * outFile = data->outFile;
    
    // create a new output file name
    std::stringstream ss;
    ss << pid;
    std::string pid_name; // = std::to_string(PIN_GetPid());
    ss >> pid_name;
    outFile->open((KnobOutputFile.Value() + "." + pid_name).c_str());

    return outFile;
}

ofstream* getOutFile(THREADID threadid) {
    // get output file
    DATA_CACHE * data = (DATA_CACHE*) PIN_GetThreadData(tls_key, threadid);
    //if (data) {
        return data->outFile; 
    //} else {
    //    return 0;
    //}
}

// After process fork, reset pid, thread local storage (tls), and output file
VOID AfterForkInChild(THREADID threadid, const CONTEXT* ctxt, VOID * arg) {
    pid_t new_pid = PIN_GetPid();
    fprintf(stdout, "After Fork in Child %u\n", new_pid);
    
    // write fork info
    ofstream * outFile = setGetOutFile(threadid, new_pid);
    *outFile << "Parent "       << pid     << " "
             << "Forked Child " << new_pid << endl;
    
    *outFile << "Address      Is Call     Time" << endl;
    pid = new_pid;
}

// This routine is executed every time a thread is created.
VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v) {
    pid_t new_pid = PIN_GetPid();
    fprintf(stdout, "Thread Start %u\n", new_pid);
    
    // write fork info
    ofstream * outFile = setGetOutFile(threadid, new_pid);
    *outFile << "Parent "               << pid          << " "
             << "Created Thread "       << PIN_GetTid() << " "
             << "With local threadid "  << threadid     << endl;
    //*outFile << endl;    
    //*outFile << setw(12) << "Address"     << " "
    //        << setw(7)  << "Is Call"     << " "
    //        << setw(12) << "Time"        << endl;

    *outFile << "Address      Is Call     Time" << endl;
    //           7fa768400c00 1    5183868400
}

// This routine is executed every time a thread is destroyed.
VOID ThreadFini(THREADID threadid, const CONTEXT *ctxt, INT32 code, VOID *v) {
    fprintf(stdout, "Thread Finish %u\n", PIN_GetTid());
    
    ofstream * outFile = getOutFile(threadid);
    //if (!outFile) {
    //    fprintf(stdout, "ofstream is null in ThreadFini\n");
    //} else {
    //*outFile << endl;
    *outFile << "Parent "              << pid          << " "
            << "Destroyed Thread "    << PIN_GetTid() << " "
            << "With local threadid " << threadid     << endl;
    //outFile.close()
    //}
}

// This function is called when the application exits
// It prints the image and address for each procedure
VOID Fini(INT32 code, VOID *v) {
    fprintf(stdout, "Application Finish %u\n", pid);
   
    THREADID threadid = PIN_ThreadId(); 
    ofstream * outFile = getOutFile(threadid);
    //if (!outFile) {
    //    fprintf(stdout, "ofstream is null in Fini\n");
    //} else {
    *outFile << "Application Finished " << pid      << " "
            << "With local threadid "  << threadid << endl;
    *outFile << setw(30) << "Procedure" << " "
            << setw(20) << "Image"     << " "
            << setw(18) << "Address"   << endl;
    
    for (RTN_COUNT * rc = RtnList; rc; rc = rc->_next) {
        *outFile << setw(30) << rc->_name  << " "
                << setw(20) << rc->_image << " "
                << setw(18) << hex << rc->_address << dec << endl;
    }
    //}
    //outFile.close()
}

//====================================================================
// Instrumentation and Analysis Routines
//====================================================================

VOID addCall(ADDRINT _address, BOOL call, UINT64 time, THREADID threadid) {
    ofstream * outFile = getOutFile(threadid);
    //if (!outFile) {
    //    fprintf(stdout, "ofstream is null in addCall\n");
    //} else {
        *outFile << setw(12)  << hex << _address << dec << " "
                 << setw(1)  <<        call            << " "
                 << setw(14) <<        time            << endl;
    //}
}

const char * StripPath(const char * path) {
    const char * file = strrchr(path, '/');
    if (file)
        return file + 1;
    else
        return path;
}

// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID *v) {
    // Allocate a structure for this routine
    RTN_COUNT * rc = new RTN_COUNT;
    rc->_address = RTN_Address(rtn);

    // Test filtering mechanism: filter out external libraries, etc.
    //if (rc->_address < 0x1000000) {
        rc->_name = RTN_Name(rtn);
        rc->_image = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());

        // Add to list of routines
        rc->_next = RtnList;
        RtnList = rc;
                
        RTN_Open(rtn);
                
        // IARG_TSC -- Type: UINT64. Time Stamp Counter value at the point of entering the analysis call.
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)addCall, IARG_ADDRINT, rc->_address, IARG_BOOL, 1, IARG_TSC, IARG_THREAD_ID, IARG_END);
        RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)addCall, IARG_ADDRINT, rc->_address, IARG_BOOL, 0, IARG_TSC, IARG_THREAD_ID, IARG_END);
        
        RTN_Close(rtn);
    //}
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[]) {
    fprintf(stdout, "entering main\n");
    
    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();
    // Initialize symbol table code, needed for rtn instrumentation
    PIN_InitSymbols();

    // Follow child processes
    PIN_AddFollowChildProcessFunction(FollowChild, 0);

    tls_key = PIN_CreateThreadDataKey(0);
    pid = PIN_GetPid();

    // Register Routine to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);

    // Register Analysis routines to be called when a thread begins/ends
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);
    
    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Register a notification handler that is called when the application forks a new process.
    PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, 0);
   
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}
