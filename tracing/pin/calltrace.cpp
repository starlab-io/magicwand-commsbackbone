/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2016 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
//
// This tool counts the number of times a routine is executed and 
// the number of instructions executed in a routine
//

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "pin.H"

using namespace std;

/* ===================================================================== */
/* Command line Switches */
/* ===================================================================== */

BOOL FollowChild(CHILD_PROCESS childProcess, VOID * userData) {
    fprintf(stdout, "before child:%u\n", getpid());
    return TRUE;
}        

/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "output/hi", "specify output file name");

//==============================================================
//  Analysis Routines
//==============================================================
// Note:  threadid+1 is used as an argument to the PIN_GetLock()
//        routine as a debugging aid.  This is the value that
//        the lock is set to, so it must be non-zero.

// lock serializes access to numThreads
PIN_LOCK lock;
OS_PROCESS_ID pid;
UINT32 numThreads;
ofstream outFile;

// Holds instruction count for a single procedure
// Lookup for later function information
typedef struct RtnCount {
    string _name;
    string _image;
    ADDRINT _address;
    struct RtnCount * _next;
} RTN_COUNT;

#define PADSIZE 27 // 64 byte line size: 64 - (8 + 1 + 8 + 4 + 4 + 4 + 8) = 27
typedef struct FunStartEnd {
    ADDRINT _address; // 8 bytes?
    bool call; // 1 for call, 0 for ret
    UINT64 time;
    THREADID threadid; // UINT32
    OS_THREAD_ID osTid; // UNIT32
    OS_PROCESS_ID osPid; // UINT32
    struct FunStartEnd * _next; // 8 bytes?
    UINT8 _pad[PADSIZE]; // pad to make sure each object is in its own data cache line
} FUN_START_END;

// Linked list of instruction counts for each routine
RTN_COUNT * RtnList = 0;
//FUN_START_END * FunList = 0;

// key for accessing TLS storage in the threads
static TLS_KEY tls_key; 

// After process fork, reset pid, thread local storage (tls), and output file
VOID AfterForkInChild(THREADID threadid, const CONTEXT* ctxt, VOID * arg) {
    fprintf(stdout, "After Fork in Child %u\n", getpid());
    pid = PIN_GetPid();    
    // reset tls information
    // TODO: I don't know if this is completely necessary
    // TODO: Also don't know if this call is thread safe after fork
    for (UINT32 t = 0; t < numThreads; t++) {
        PIN_SetThreadData(tls_key, 0, t);
    }
    // redo file handle
    std::stringstream ss;
    ss << pid;
    std::string pid_name; // = std::to_string(PIN_GetPid());
    ss >> pid_name;
    outFile.close();
    outFile.open((KnobOutputFile.Value() + "." + pid_name).c_str());
}

// This routine is executed every time a thread is created.
VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v) {
    FUN_START_END * fun = new FUN_START_END;
    fun->_address = (ADDRINT) 0;
    fun->call = 1;
    //fun->time = IARG_TSC; // WRONG
    fun->time = 0; // TODO: Figure out how to get timestamp in thread start
    fun->threadid = threadid;
    fun->osTid = PIN_GetTid();
    fun->osPid = PIN_GetPid();

    PIN_GetLock(&lock, threadid+1);
    // threadid can be reused, so take the top so far
    numThreads = MAX(numThreads, threadid + 1);
    PIN_ReleaseLock(&lock);

    // TODO: Does a call to PIN_GetThreadData return null when nothing has been added?
    fun->_next = (FUN_START_END*) PIN_GetThreadData(tls_key, threadid);
    PIN_SetThreadData(tls_key, fun, threadid);
}

// This routine is executed every time a thread is destroyed.
VOID ThreadFini(THREADID threadid, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    FUN_START_END * fun = new FUN_START_END;
    fun->_address = (ADDRINT) 0;
    fun->call = 0;
    //fun->time = IARG_TSC; // WRONG
    fun->time = 0; // TODO: Figure out how to get timestamp in thread start
    fun->threadid = threadid;
    fun->osTid = PIN_GetTid();
    fun->osPid = PIN_GetPid();
    
    fun->_next = (FUN_START_END*) PIN_GetThreadData(tls_key, threadid);
    PIN_SetThreadData(tls_key, fun, threadid);
}

VOID addCall(ADDRINT _address, BOOL call, UINT64 time, THREADID threadid) {
    // Create new structure for data
    FUN_START_END * fun = new FUN_START_END;
    fun->_address = _address;
    fun->call = call;
    fun->time = time;
    fun->threadid = threadid;
    fun->osTid = PIN_GetTid();
    fun->osPid = PIN_GetPid();

    fun->_next = (FUN_START_END*) PIN_GetThreadData(tls_key, threadid);
    PIN_SetThreadData(tls_key, fun, threadid);
    
    // Get lock and update linked list of data
//    PIN_GetLock(&lock, threadid+1);
//    fun->_next = FunList;
//    FunList = fun;
//    PIN_ReleaseLock(&lock);
}

const char * StripPath(const char * path) {
    const char * file = strrchr(path,'/');
    if (file)
        return file+1;
    else
        return path;
}

//====================================================================
// Instrumentation Routines
//====================================================================

// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID *v)
{
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

// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID *v) {
    fprintf(stdout, "Application Finish %u\n", getpid());

    outFile << setw(30) << "Procedure" << " "
            << setw(20) << "Image"     << " "
            << setw(18) << "Address"   << endl;

    for (RTN_COUNT * rc = RtnList; rc; rc = rc->_next) {
        outFile << setw(30) << rc->_name  << " "
                << setw(20) << rc->_image << " "
                << setw(18) << hex << rc->_address << dec << endl;
    }

    outFile << endl;

    outFile << setw(18) << "Address"     << " "
            << setw(7)  << "Is Call"     << " "
            << setw(12) << "Time"        << " "
            << setw(10) << "LocalID"     << " "
            << setw(10) << "Thread ID"   << " "
            << setw(10) << "Process ID"  << endl;
    
    for (UINT32 t = 0; t < numThreads; t++) {
        for (FUN_START_END * fun = (FUN_START_END*) PIN_GetThreadData(tls_key, t); fun; fun = fun->_next) {
            outFile << setw(18) << hex << fun->_address << dec << " "
                    << setw(7)  << fun->call     << " "
                    << setw(12) << fun->time     << " "
                    << setw(10) << fun->threadid << " "
                    << setw(10) << fun->osTid    << " "
                    << setw(10) << fun->osPid    << endl;
        }
    }

}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR("This Pintool writes a trace of function entrance and exit\nwith their address, timestamp, and internal threadid\n" +
            KNOB_BASE::StringKnobSummary() + "\n");
//    cerr << "This Pintool writes a trace of function entrance and exit" << endl;
//    cerr << "with their address, timestamp, and internal threadid" << endl;
//    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[]) {
    fprintf(stdout, "entering main\n");
    // Initialize the pin lock
    PIN_InitLock(&lock);
    
    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();
    // Initialize symbol table code, needed for rtn instrumentation
    PIN_InitSymbols();

    // Follow child processes
    PIN_AddFollowChildProcessFunction(FollowChild, 0);

// TODO: Need a different output file for child and forked processes 
    pid = PIN_GetPid();    
    numThreads = 0;

    std::stringstream ss;
    ss << PIN_GetPid();
    std::string pid_name; // = std::to_string(PIN_GetPid());
    ss >> pid_name;
    outFile.open((KnobOutputFile.Value() + "." + pid_name).c_str());
    //outFile.open("proccount.out");
    //out = fopen(KnobOutputFile.Value().c_str(), "w");

    fprintf(stdout, "current process:%s\n", pid_name.c_str());

    tls_key = PIN_CreateThreadDataKey(0);

    // Register Routine to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);

    // Register Analysis routines to be called when a thread begins/ends
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);
    
    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Register a notification handler that is called when the application
    // forks a new process.
    //PIN_AddForkFunction(FPOINT_BEFORE, BeforeFork, 0);	
    //PIN_AddForkFunction(FPOINT_AFTER_IN_PARENT, AfterForkInParent, 0);
    PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}
