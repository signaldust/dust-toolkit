
#pragma once

#include "defs.h"

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#endif

// This is written to assume either Windows or POSIX
namespace dust
{

    // run another process, redirect input/output/stderr
    struct SlaveProcess
    {
#ifndef _WIN32
        pid_t   slavePid = 0;
        
        int     slaveInput  = -1;   // fd for writing to slave
        int     slaveOutput = -1;   // fd for reading from slave
#else
        HANDLE  slaveHandle = 0;
        DWORD   slaveProcGroup = 0;
        HANDLE  slaveInput = 0;
        HANDLE  slaveOutput = 0;
#endif        
        // update sets this to the exit status
        // -1 if child was killed by a signal
        int     exitStatus  = 0;
        
        // these should be set before starting the slave
        // note that we used args[0] as the command for now
        std::vector<std::string>    args;

        // this just makes the interface easier
        void pushArg(const std::string & a) { args.push_back(a); }
    
        // this starts the slave process
        void start()
        {
#ifdef _WIN32
            // if slave is already running, bail out
            if(slaveHandle) return;
            
            SECURITY_ATTRIBUTES saAttr = {};
            saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
            saAttr.bInheritHandle = TRUE; 
            
            HANDLE childInput = 0;
            HANDLE childOutput = 0;
            HANDLE childError = 0;

            if(!CreatePipe(&childInput, &slaveInput, &saAttr, 0))
            {
                dust::debugPrint("SlaveProces::start(): CreatePipe(i) failed\n");
            }
            if(!CreatePipe(&slaveOutput, &childOutput, &saAttr, 0))
            {
                dust::debugPrint("SlaveProces::start(): CreatePipe(i) failed\n");
            }

            // could error check these but whatever
            SetHandleInformation(slaveInput, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(slaveOutput, HANDLE_FLAG_INHERIT, 0);

            // duplicate child-side for STDERR
            HANDLE currentProcess = GetCurrentProcess();
            if(!DuplicateHandle(
                currentProcess, childOutput,
                currentProcess, &childError,
                0, TRUE, DUPLICATE_SAME_ACCESS))
            {
                dust::debugPrint("SlaveProcess::start(): DuplicateHandle failed\n");
            }

            PROCESS_INFORMATION procInfo;
            STARTUPINFOA startInfo;

            ZeroMemory(&procInfo, sizeof(procInfo));
            ZeroMemory(&startInfo, sizeof(startInfo));
            startInfo.cb = sizeof(STARTUPINFOA);
            startInfo.hStdError = childError;
            startInfo.hStdOutput = childOutput;
            startInfo.hStdInput = childInput;
            startInfo.wShowWindow = SW_HIDE;
            startInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

            std::vector<char> cmdLine;
            for(auto & ch : args[0]) cmdLine.push_back(ch);
            for(int i = 1; i < args.size(); ++i)
            {
                cmdLine.push_back(' ');
                for(auto & ch : args[i]) cmdLine.push_back(ch);
            }
            cmdLine.push_back(0);

            if(!CreateProcessA(0, cmdLine.data(), 0, 0, TRUE,
                CREATE_NEW_PROCESS_GROUP, 0, 0, &startInfo, &procInfo))
            {
                dust::debugPrint("SlaveProcess::start(): CreateProcess failed\n");
                CloseHandle(slaveInput);
                CloseHandle(slaveOutput);
            }

            CloseHandle(childInput);
            CloseHandle(childOutput);
            CloseHandle(childError);
            CloseHandle(procInfo.hThread);

            // we'll keep the process handle
            slaveHandle = procInfo.hProcess;
            slaveProcGroup = procInfo.dwProcessId;
#else
            // if slave is already running, bail out
            if(slavePid) return;
            
            // create two pipes
            int iPipe[2];   // slave input
            int oPipe[2];   // slave output/stderr
            
            if(pipe(iPipe))
            { 
                dust::debugPrint("SlaveProcess::start(): pipe(i) failed\n");
            }
            if(pipe(oPipe))
            {
                dust::debugPrint("SlaveProcess::start(): pipe(o) failed\n");
            }
        
            // fork the process
            pid_t pid = fork();
            
            if(pid)
            {
                // parent process
                slavePid = pid;
                
                // store the pipe descriptors we want
                // close the other ends points
                close(iPipe[0]);
                slaveInput = iPipe[1];
                slaveOutput = oPipe[0];
                close(oPipe[1]);
                
                // make output non-blocking
                int outFlags = fcntl(slaveOutput, F_GETFL, 0);
                if(-1 == fcntl(slaveOutput, F_SETFL, outFlags | O_NONBLOCK))
                {
                    dust::debugPrint("Failed to set non-blocking?!?");
                }
            }
            else
            {
                // child process: redirect io
                dup2(iPipe[0], 0);  // stdin from iPipe
                dup2(oPipe[1], 1);  // stdout to oPipe
                dup2(oPipe[1], 2);  // stderr to oPipe
                
                // explicitly close all other files
                int maxFiles = sysconf(_SC_OPEN_MAX);
                for(int p = 3; p < maxFiles; ++p) close(p);
                
                // build a vector of c-strings
                std::vector<char*>  carg;
                // we can safely ignore const here
                for(auto & s : args) carg.push_back((char*) s.c_str());
                // null-terminate the list
                carg.push_back(0);
        
                // use path searching version
                execvp(args[0].c_str(), carg.data());
                
                // if we end up here, then exec failed
                abort();
            }
#endif
        }

        // return true if success, false if error occurs
        bool sendInput(const char * bytes, unsigned nBytes)
        {
            unsigned offset = 0;
            while(offset < nBytes)
            {
#ifdef _WIN32
                DWORD wrote = 0;
                if(!WriteFile(slaveInput,
                    bytes + offset, nBytes - offset, &wrote, 0)) return false;
                offset += wrote;
#else
                ssize_t wrote = write(slaveInput, bytes + offset, nBytes - offset);
                if(wrote < 1) return false,
                offset += wrote;
#endif
            }
            return true;
        }
        
        // this closes the pipe associated with slave input
        // it is safe to call this multiple times
        void closeInput()
        {
#ifdef _WIN32
            if(slaveInput)
            {
                CloseHandle(slaveInput);
                slaveInput = 0;
            }
#else
            if(slaveInput > 0)
            {
                close(slaveInput);
                slaveInput = -1;
            }
#endif
        }
        
#ifdef _WIN32
        bool isAlive() { return 0 != slaveHandle; }
        void kill()
        {
            if(!slaveHandle) return;

            // we need to attach to the process or this won't work
            AttachConsole(slaveProcGroup);
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, slaveProcGroup);
            FreeConsole();
        }
#else
        bool isAlive() { return 0 != slavePid; }
        void kill(int signal = SIGTERM)
        {
            if(slavePid) ::kill(slavePid, signal);
        }
#endif 
        // append output into a vector and cleanup after a dead slave
        // returns true if slave is still alive, false if dead
        bool update(std::vector<char> & output)
        {
#ifdef _WIN32
            if(!slaveHandle) return false;
#else
            if(!slavePid) return false;
#endif
            const unsigned bufSize = 1024;
            char buf[bufSize];
            
            // figure out if child is actually alive
#ifdef _WIN32
            bool alive = (WaitForSingleObject(slaveHandle, 0) == WAIT_TIMEOUT);
#else
            int status = 0;
            bool alive = !waitpid(slavePid, &status, WNOHANG);
#endif
            
            // then read output; do this after wait() to make sure
            // we get all output even after the child has finished
            //
            // NOTE: we don't actually wait for EOF, since the pipe
            // might be held open by some child process of our slave
            // and we can't explicitly force those to finish
            while(true)
            {
#ifdef _WIN32
                // This funky dance is because PeekNP doesn't remove data
                DWORD n = 0;
                if(!PeekNamedPipe(slaveOutput, 0, 0, 0, &n, 0)) break;
                if(!n) break;
                if(n > bufSize) n = bufSize;
                if(!ReadFile(slaveOutput, buf, n, &n, 0)) break;
#else
                int n = read(slaveOutput, buf, bufSize);
#endif
                // end of file or error, we can just bail out either way
                if(n <= 0) break;
                
                // copy output to vector (could read directly too, but whatever)
                for(int i = 0; i < n; ++i)
                { if(buf[i] != '\r') output.push_back(buf[i]); }
            }
    
            // if child was alive, we're done
            if(alive) return true;
            
            // otherwise get status and cleanup
            closeInput();
#ifdef _WIN32
            DWORD exitCode = 0;
            if(GetExitCodeProcess(slaveHandle, &exitCode) == FALSE)
                exitStatus = -1;
            else exitStatus = exitCode;
            
            CloseHandle(slaveOutput);
            CloseHandle(slaveHandle);
            slaveHandle = 0;
#else
            if(!WIFEXITED(status)) exitStatus = -1;
            else exitStatus = WEXITSTATUS(status);
            
            close(slaveOutput);
            slavePid = 0;
#endif
            return false;
        }
    };
};
