#include <windows.h>
#include <stdio.h>
#include <errno.h>
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

void installService(char *path)
{
	if (path) {
		char	fullPath[8096*2] = "";

		_snprintf(fullPath, sizeof (fullPath), "\"%s\\icecastService.exe\" \"%s\"", path, path);
		SC_HANDLE handle = OpenSCManager( NULL, NULL, SC_MANAGER_ALL_ACCESS );
		if (handle == NULL)
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
				handle,
			"Icecast",
			"Icecast Media Server",
			GENERIC_READ | GENERIC_EXECUTE,
			SERVICE_WIN32_OWN_PROCESS,
			SERVICE_AUTO_START,
			SERVICE_ERROR_IGNORE,
			fullPath,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL
		);
		if (handle == NULL)
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
			CloseServiceHandle (handle);
			return;
		}

		printf("Service Installed\n");
		CloseServiceHandle (service);
		CloseServiceHandle (handle);
	}
}
void removeService()
{
	SC_HANDLE handle = OpenSCManager( NULL, NULL, SC_MANAGER_ALL_ACCESS );
	if (handle == NULL)
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

	SC_HANDLE service = OpenService(handle, "Icecast", DELETE);
	if (service) {
		DeleteService(service);
		printf("Service Removed\n");
	}
	else
		printf("Service not found\n");
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
 
   hStatus = RegisterServiceCtrlHandler("Icecast", (LPHANDLER_FUNCTION)ControlHandler); 
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
	char*	argv2[3];

	argv2[0] = "icecastService.exe";
	argv2[1] = "-c";
	argv2[2] = "icecast.xml";

	int ret = mainService(argc2, (char **)argv2);

	ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	ServiceStatus.dwWin32ExitCode = -1;
	SetServiceStatus(hStatus, &ServiceStatus);
	return; 
}


void main(int argc, char **argv) 
{

	bool matched  = false;
	if (argv[0]) {
		if (argv[1]) {
			if (!strcmp(argv[1], "install")) {
				installService(argv[2]);
				matched = true;
			}
			if (!strcmp(argv[1], "remove")) {
				removeService();
				matched = true;
			}
		}
	}
	if (matched) {
		return;
	}
	_chdir(argv[1]);

	SERVICE_TABLE_ENTRY ServiceTable[2];
	ServiceTable[0].lpServiceName = "Icecast Server";
	ServiceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceMain;

	ServiceTable[1].lpServiceName = NULL;
	ServiceTable[1].lpServiceProc = NULL;
	// Start the control dispatcher thread for our service
	StartServiceCtrlDispatcher(ServiceTable);  
}
