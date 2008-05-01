#include <config.h>
#include <stdio.h>
#include <direct.h>
extern "C" {
#include "thread/thread.h"
#include "avl/avl.h"
#include "log/log.h"
#include "global.h"
#include "httpp/httpp.h"
#include "net/sock.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
}

// Issues to be wary of. Careful of the runtime you use, I've had printf and similar routines
// crash because of this on apparently valid strings. some weird thing related to checking for
// multiple byte characters.  DeleteService only marks a service for deletion, and the docs
// are unclear on the cases that lead to purging however a reboot should do it.

SERVICE_STATUS          ServiceStatus; 
SERVICE_STATUS_HANDLE   hStatus; 
 
void  ServiceMain(int argc, char** argv); 
void  ControlHandler(DWORD request); 
int InitService();
extern "C" int mainService(int argc, char **argv);

int InitService() 
{ 
   int result = 0;
   return(result); 
}

void installService (const char *path)
{
	if (path) {
		char	buffer[8096*2] = "\"";
        int len = GetModuleFileName (NULL, buffer+1, sizeof (buffer)-1);

		_snprintf (buffer+len+1, sizeof (buffer)-len, "\" \"%s\"", path);

		SC_HANDLE manager = OpenSCManager( NULL, NULL, SC_MANAGER_ALL_ACCESS );
		if (manager == NULL)
		{
			LPVOID lpMsgBuf;
			FormatMessage( 
					FORMAT_MESSAGE_ALLOCATE_BUFFER | 
					FORMAT_MESSAGE_FROM_SYSTEM | 
					FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL,
					GetLastError(),
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
					(LPTSTR) &lpMsgBuf,
					0,
					NULL 
					);

			printf ("OpenSCManager: %s\n", (LPCTSTR)lpMsgBuf);
			LocalFree( lpMsgBuf );
			return;
		}

		SC_HANDLE service = CreateService(
			manager,
			PACKAGE_STRING,
			PACKAGE_STRING " Streaming Media Server",
			GENERIC_READ | GENERIC_EXECUTE,
			SERVICE_WIN32_OWN_PROCESS,
			SERVICE_AUTO_START,
			SERVICE_ERROR_IGNORE,
			buffer,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL
		);
		if (service == NULL)
		{
			LPVOID lpMsgBuf;
			FormatMessage( 
					FORMAT_MESSAGE_ALLOCATE_BUFFER | 
					FORMAT_MESSAGE_FROM_SYSTEM | 
					FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL,
					GetLastError(),
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
					(LPTSTR) &lpMsgBuf,
					0,
					NULL 
					);

			printf ("CreateService: %s\n", (LPCTSTR)lpMsgBuf);
            LocalFree( lpMsgBuf );
			CloseServiceHandle (manager);
			return;
		}

		printf ("Service Installed\n");
		CloseServiceHandle (service);
		CloseServiceHandle (manager);
	}
}
void removeService()
{
	SC_HANDLE manager = OpenSCManager( NULL, NULL, SC_MANAGER_ALL_ACCESS );
	if (manager == NULL)
	{
		LPVOID lpMsgBuf;
		FormatMessage( 
				FORMAT_MESSAGE_ALLOCATE_BUFFER | 
				FORMAT_MESSAGE_FROM_SYSTEM | 
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
				(LPTSTR) &lpMsgBuf,
				0,
				NULL 
				);

		printf ("OpenSCManager: %s\n", (LPCTSTR)lpMsgBuf);
		LocalFree( lpMsgBuf );
		return;
	}

	SC_HANDLE service = OpenService (manager, PACKAGE_STRING, DELETE);
	if (service) {
		DeleteService(service);
		printf ("Service Removed\n");
        CloseServiceHandle (service);
	}
	else
		printf ("Service not found\n");
    CloseServiceHandle (manager);
}
void ControlHandler(DWORD request) 
{ 
   switch(request) { 
      case SERVICE_CONTROL_STOP: 
		global.running = ICE_HALTING;
		ServiceStatus.dwWin32ExitCode = 0; 
		ServiceStatus.dwCurrentState = SERVICE_STOPPED; 
		SetServiceStatus (hStatus, &ServiceStatus);
		return; 
 
      case SERVICE_CONTROL_SHUTDOWN: 
		global.running = ICE_HALTING;
		ServiceStatus.dwWin32ExitCode = 0; 
		ServiceStatus.dwCurrentState = SERVICE_STOPPED; 
		SetServiceStatus (hStatus, &ServiceStatus);
		return; 
      default:
		break;
    } 
 
    // Report current status
    SetServiceStatus (hStatus, &ServiceStatus);
 
    return; 
}

void ServiceMain(int argc, char** argv) 
{ 
   int error; 
 
   ServiceStatus.dwServiceType = SERVICE_WIN32; 
   ServiceStatus.dwCurrentState = SERVICE_START_PENDING; 
   ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
   ServiceStatus.dwWin32ExitCode = 0; 
   ServiceStatus.dwServiceSpecificExitCode = 0; 
   ServiceStatus.dwCheckPoint = 0; 
   ServiceStatus.dwWaitHint = 0; 
 
   hStatus = RegisterServiceCtrlHandler(PACKAGE_STRING, (LPHANDLER_FUNCTION)ControlHandler); 
   if (hStatus == (SERVICE_STATUS_HANDLE)0) { 
      // Registering Control Handler failed
      return; 
   }  
   // Initialize Service 
   error = InitService(); 
   if (error) {
      // Initialization failed
      ServiceStatus.dwCurrentState = SERVICE_STOPPED; 
      ServiceStatus.dwWin32ExitCode = -1; 
      SetServiceStatus(hStatus, &ServiceStatus); 
      return; 
   } 
   // We report the running status to SCM. 
   ServiceStatus.dwCurrentState = SERVICE_RUNNING; 
   SetServiceStatus (hStatus, &ServiceStatus);
 
   /* Here we do the work */

   	int		argc2 = 3;
	char*	argv2 [3];

    argv2 [0] = argv[0];
    argv2 [1] = "-c";
    if (argc < 2)
        argv2 [2] = "icecast.xml";
    else
        argv2 [2] = argv[1];

	int ret = mainService(argc2, (char **)argv2);

	ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	ServiceStatus.dwWin32ExitCode = -1;
	SetServiceStatus(hStatus, &ServiceStatus);
	return; 
}


void main(int argc, char *argv[]) 
{
    if (argc < 2)
    {
        printf ("Usage: icecastservice [remove] | [install <path>]\n");
        return;
    }
    if (!strcmp(argv[1], "install"))
    {
        if (argc > 2)
            installService(argv[2]);
        else
            printf ("install requires a path arg as well\n");
        Sleep (1000);
        return;
    }
    if (!strcmp(argv[1], "remove") || !strcmp(argv[1], "uninstall"))
    {
        removeService();
		printf ("service removed, may require a reboot\n");
        Sleep (1000);
        return;
    }

   if (_chdir(argv[1]) < 0)
   {
       printf ("unable to change to directory %s\n", argv[1]);
       Sleep (1000);
       return;
   }

    SERVICE_TABLE_ENTRY ServiceTable[2];
    ServiceTable[0].lpServiceName = PACKAGE_STRING;
    ServiceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceMain;

    ServiceTable[1].lpServiceName = NULL;
    ServiceTable[1].lpServiceProc = NULL;
    // Start the control dispatcher thread for our service
    StartServiceCtrlDispatcher(ServiceTable);
}
