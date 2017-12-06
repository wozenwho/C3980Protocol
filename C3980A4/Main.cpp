/*
PROGRAM HEADER HERE
*/
#define STRICT
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "Header.h"
#include "Main.h"
#include "Receive.h"
#include "Transmit.h"
#include "Print.h"
#include "crc.h"

char programName[] = "C3980 A4";
char filePathBuffer[128];
LPCSTR lpszCommName = "COM1";
DCB deviceContext;

HANDLE port;
HANDLE timerThread;

//OpenFile Global Variables
HANDLE fileHandle;
OPENFILENAME ofn;
char inputFileBuffer[32768];
OVERLAPPED ol;
DWORD g_BytesTransferred;

HANDLE idleThread;
HANDLE readInputBufferThread;

HWND hwnd;
boolean connectMode = false;
int printRow = 0;
int printColumn = 0;

//Function Headers
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);
VOID startTimer(unsigned int time);
VOID CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD);
VOID Idle();
VOID Acknowledge();
BOOL Validation(uint32_t receivedCRC, char* input);
void bidForLine();
void sendEnq();
DWORD readThread(LPDWORD);
BOOL writeToPort(char*, DWORD);


extern bool timeout = false;
extern bool linkedReset = false;
//FILE * outputBuffer = NULL;

char inputBuffer[518] = { 0 };
extern bool rvi = false;


//bool timeout;
//char* inputBuffer;



//Initialize OPENFILENAME



int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hprevInstance, LPSTR lspszCmdParam, int nCmdShow)
{
	COMMTIMEOUTS ct = { 0 };
	char settings[] = "9600,N,8,1"; //"9600,N,8,1"

	MSG Msg;
	WNDCLASSEX Wcl;
	PAINTSTRUCT paintstruct;
	HDC hdc = BeginPaint(hwnd, &paintstruct);

	//FILE * s;
	Wcl.cbSize = sizeof(WNDCLASSEX);
	Wcl.style = CS_HREDRAW | CS_VREDRAW;
	Wcl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	Wcl.hIconSm = NULL;
	Wcl.hCursor = LoadCursor(NULL, IDC_ARROW);

	Wcl.lpfnWndProc = WndProc;
	Wcl.hInstance = hInst;
	Wcl.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	Wcl.lpszClassName = programName;

	Wcl.lpszMenuName = "commandMenu";
	Wcl.cbClsExtra = 0;
	Wcl.cbWndExtra = 0;
	if (!RegisterClassEx(&Wcl))
		return 0;

	//sets memory of OPENFILEDIALOG
	memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFile = filePathBuffer;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(filePathBuffer);
	ofn.lpstrFilter = "All\0*.*\Text\0*.TXT\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	ol = { 0 };

	hwnd = CreateWindow(programName, programName, WS_OVERLAPPEDWINDOW, 10, 10, windowWidth, windowHeight, NULL, NULL, hInst, NULL);
	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	if ((port = CreateFile("com1", GENERIC_READ | GENERIC_WRITE, 0,
		NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL))
		== INVALID_HANDLE_VALUE)
	{
		MessageBox(NULL, "Error opening COM port:", "", MB_OK);
		return FALSE;
	}

	ct.ReadIntervalTimeout = 500;
	ct.ReadTotalTimeoutMultiplier = 500;
	ct.ReadTotalTimeoutConstant = 500;
	ct.WriteTotalTimeoutMultiplier = 500;
	ct.WriteTotalTimeoutConstant = 500;
	SetCommTimeouts(port, &ct);

	//setup device context settings
	if (!SetupComm(port, 8, 8)) { //is it supposed to be 8?
		MessageBox(hwnd, "setupComm failed", "", NULL);
	}

	if (!GetCommState(port, &deviceContext)) {
		MessageBox(hwnd, "getCommState failed", "", NULL);
	}

	if (BuildCommDCB(settings, &deviceContext)) {
		deviceContext.fOutxCtsFlow = FALSE;
		deviceContext.fOutxDsrFlow = FALSE;
		deviceContext.fDtrControl = DTR_CONTROL_DISABLE;
		deviceContext.fOutX = FALSE;
		deviceContext.fInX = FALSE;
		deviceContext.fRtsControl = RTS_CONTROL_DISABLE;
	}
	else {
		MessageBox(hwnd, "buildCommDCB failed", "", NULL);
	}

	if (!SetCommState(port, &deviceContext)) {
		MessageBox(hwnd, "setCommState failed", "", NULL);
	}


	while (GetMessage(&Msg, NULL, 0, 0))
	{
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}

	return Msg.wParam;
}

VOID startTimer(unsigned int time)
{
	timeout = false;
	SetTimer(hwnd, TIMER_TEST, time, TimerProc);
}

VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
	timeout = true;
	KillTimer(hwnd, TIMER_TEST);
	//MessageBox(hwnd, "Timed out.", "", NULL);
}

VOID CALLBACK FileIOCompletionRoutine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransferred, LPOVERLAPPED lpOverlapped)
{
	printf(TEXT("Error code:\t%x\n"), dwErrorCode);
	printf(TEXT("Number of bytes: \t%x\n"), dwNumberOfBytesTransferred);
	g_BytesTransferred = dwNumberOfBytesTransferred;
}

//Can we move WndProc up to be right below WinMain -Wilson
LRESULT CALLBACK WndProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	HDC hdc;
	PAINTSTRUCT paintstruct;
	DWORD threadId; //unused?

	//File Input variables
	DWORD dwBytesRead = 0;

	switch (Message)
	{
		//Switch case to handle menu buttons
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
			//Connect menu button pressed, should probably connect before setting connectMode=true
		case (MENU_CONNECT):
			if (!connectMode) {
				connectMode = true;

				if ((idleThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Idle, NULL, 0, 0)) == INVALID_HANDLE_VALUE) {
					/* handle error */
					//return 0;
					MessageBox(hwnd, "Could not create idleThread", "", NULL);
				} /* end if (error creating read thread) */
				if ((readInputBufferThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)readThread, NULL, 0, 0)) == INVALID_HANDLE_VALUE) {
				}
				//if (ResumeThread(readInputBufferThread) == -1) {
				//	MessageBox(hwnd, "could not resume readInputBufferThread, my lord", "", NULL);
				//}
			}
			break;
			//Disconnect menu button pressed
		case (MENU_DISCONNECT):
			connectMode = false;
			break;
			//Quit menu button pressed
		case (MENU_QUIT):
			PostQuitMessage(0);
			break;
			//File menu button pressed
		case (MENU_FILE):
			if (GetOpenFileName(&ofn) == TRUE)
			{
				fileHandle = CreateFile(ofn.lpstrFile, GENERIC_READ, 0, (LPSECURITY_ATTRIBUTES)NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, (HANDLE)NULL);

				if (fileHandle == INVALID_HANDLE_VALUE) {
					MessageBox(hwnd, "I could not open the file master.", "", NULL);
				}

				if (!ReadFileEx(fileHandle, inputFileBuffer, sizeof(inputFileBuffer) - 1, &ol, FileIOCompletionRoutine))
				{
					//DisplayError(TEXT("ReadFile"));
					MessageBox(hwnd, inputFileBuffer, "", NULL);
					//printf("%s", inputFileBuffer);
				}
				//if (dwBytesRead > 0 && dwBytesRead <= sizeof(inputFileBuffer) - 1)
				//{
					//sprintf_s(inputFileBuffer, "%s", fileHandle);
				//}
			}
			break;
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(hwnd, &paintstruct);
		break;
	case WM_CHAR:
		switch (wParam) {
		case 114:
			rvi = true;
			break;
		}
		break;
	case WM_DESTROY:

		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, Message, wParam, lParam);
	}
	return 0;
}

VOID Idle()
{
	while (true)
	{
		if (linkedReset != false)
		{
			startTimer(10000);
			while (!timeout)
			{
				//MessageBox(hwnd, "idle", "", MB_OK);

				if (inputBuffer[0] == 22 && inputBuffer[1] == 5 && strlen(inputBuffer) > 0)
				{
					Acknowledge();
					break;
					//memset(inputBuffer, 0, 518);
				}
				if (timeout)
				{
					linkedReset = false;
				}
			}
		}
		if (inputBuffer[0] == 22 && inputBuffer[1] == 5 && strlen(inputBuffer) > 0)
		{
			Acknowledge();
			//memset(inputBuffer, 0, 518);
		}
		else if (strlen(inputFileBuffer) > 0)
		{
			/*
			uint32_t test = CRC::Calculate(inputFileBuffer, st, CRC::CRC_32());
			bool passed = Validation(test, inputFileBuffer);
			unsigned char bytes[4];
			unsigned long n = test;

			bytes[0] = (n >> 24) & 0xFF;
			bytes[1] = (n >> 16) & 0xFF;
			bytes[2] = (n >> 8) & 0xFF;
			bytes[3] = n & 0xFF;
			*/
			sendEnq();
			bidForLine();
			fprintf(stderr, "asdf");
		}
	}
}

VOID Acknowledge()
{
	DWORD bytesWritten;
	control[0] = 22;
	control[1] = 6;
	bool write = writeToPort(control, 2);
	//WriteFile(port, control, sizeof(control), &bytesWritten, NULL);
	Receive();
}



VOID sendEnq()
{
	DWORD dwBytesWritten;
	char enq[2];
	enq[0] = 22;
	enq[1] = 5;
	//PurgeComm(port, PURGE_RXCLEAR);
	bool write = writeToPort(enq, 2);
	//bool bwrite = WriteFile(port, enq, 2, &dwBytesWritten, NULL);
	if (!write) {
		MessageBox(hwnd, "i suck  at writefile", "", MB_OK);
	}
	//PurgeComm(port, PURGE_RXCLEAR);
}

VOID bidForLine()
{
	//MessageBox(hwnd, "Calling bidForLine()", "", NULL);
	startTimer(5000);
	while (timeout != true)
	{
		//MessageBox(hwnd, "bid", "", MB_OK);
		if (inputBuffer[0] == 22 && inputBuffer[1] == 6)
		{
			memset(inputBuffer, 0, 518);
			prepareToSend(inputFileBuffer, port);
			eot = false;
			memset(inputBuffer, 0, 518);
			timeout = true;
		}
	}
	linkedReset = true;
	return;
}

//thread function to read from input buffer
DWORD readThread(LPDWORD lpdwParam1)
{
	DWORD nBytesRead = 0;
	DWORD dwEvent, dwError;
	COMSTAT cs;
	OVERLAPPED osReader = { 0 };
	char a[10];

	SetCommMask(port, EV_RXCHAR);

	//temp bool used for read loop
	while (connectMode) {
		if (WaitCommEvent(port, &dwEvent, NULL))
		{
			//MessageBox(hwnd, "I have received an event, m'lord!", "", NULL);
			ClearCommError(port, &dwError, &cs);

			osReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			if (osReader.hEvent == NULL)
			{
				MessageBox(hwnd, "I COULD NOT CREATE HEVENT MASTER", "", NULL);
				return 0;
			}

			if ((dwEvent & EV_RXCHAR) && cs.cbInQue)
			{
				if (!ReadFile(port, inputBuffer, sizeof(inputBuffer), &nBytesRead, &osReader)) //need receive buffer to be extern to access
				{
					//error case, handle error here
					int i = 0;
				}
				else
				{
					//handle success
					int u = 0;
				}
			}
		}
		PurgeComm(port, PURGE_RXCLEAR);
	}
	return nBytesRead;
}

BOOL readFromBuffer()
{
	return FALSE;
}

BOOL writeToPort(char* writeBuffer, DWORD dwNumToWrite)
{
	OVERLAPPED osWrite = { 0 };
	DWORD dwWritten;
	DWORD dwRes;
	BOOL result;

	osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (osWrite.hEvent == NULL)
	{
		return FALSE;
	}

	//Issue write
	if (!WriteFile(port, writeBuffer, dwNumToWrite, &dwRes, &osWrite))
	{

		if (GetLastError() != ERROR_IO_PENDING)
		{
			result = FALSE;
		}
		else {
			//Write is pending
			dwRes = WaitForSingleObject(osWrite.hEvent, 2000);
			switch (dwRes)
			{
			case WAIT_OBJECT_0:
				if (!GetOverlappedResult(port, &osWrite, &dwWritten, FALSE))
					result = FALSE;
				else
					result = TRUE;
				break;
			default:
				result = FALSE;
				break;
			}
		}
	}
	else  //WriteFile succeeded
	{
		result = TRUE;
	}
	return result;
}


