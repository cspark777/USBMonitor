#ifndef ___USB_FILTER_H___

#define ___USB_FILTER_H___

//#define ___DRIVER_TEST

typedef unsigned short	WORD;
typedef ULONG			DWORD;
typedef char			bool;

#define true 1
#define false 0

#define BEACON_USB_FILTER	L"\\??\\BeaconMS_USBfilter"
#define MAX_PACKET	64
#define MAX_BUFFER	32*1024
#define MAX_CAPTURE_BUFFER 128*1024

enum deviceState {
        STATE_INITIALIZED,
        STATE_STARTING,
        STATE_STARTED,
        STATE_START_FAILED,
        STATE_STOPPED,  /* implies device was previously started successfully  */
        STATE_SUSPENDED,
        STATE_REMOVING,
        STATE_REMOVED
};

enum DERECTION {DIRECTION_IN = 0x00000001, DIRECTION_OUT};
enum capture {CAPTURE_START, CAPTURE_STOP};

#define FILTER_TAG (ULONG)'drib'
#undef ExAllocatePool
#define ExAllocatePool(type, size) \
            ExAllocatePoolWithTag (type, size, FILTER_TAG)

#define DEVICE_EXTENSION_SIGNATURE 'DBSU'
#define FILTER_DEVICE_OBJ	0x00000000
#define CONTROL_DEVICE_OBJ	0x00000001

struct timeval 
	{
    long    tv_sec;         ///< seconds
    long    tv_usec;        ///< microseconds
	};

typedef struct _PKHDR {
	WORD length;
	WORD code;
	WORD offset;
	WORD bFlag;		// buffer index
	struct timeval tv;
	} PKHDR;

typedef struct _PKHDR_MERGE {
	WORD length;
	WORD code;
	DWORD offset;
	struct timeval tv;
	} PKHDR_MERGE;

typedef struct _EVENT_STRUCT {
	HANDLE hPacket;	/// packet capture
	HANDLE hRemove;	/// surprise removal
	} EVENT_STRUCT;

typedef enum AT_RESPONSE 
	{
	START_CR, 
	START_LF, 
	STOP_CR, 
	STOP_LF, 
	SEPARATOR
	};

UNICODE_STRING DevlinkName;
static const ULONG DRIVER_VERSION = 0x0090;
static const WORD wWriteFlag = 0xE110;
static const WORD wReadFlag = 0xE210;
static const int nPKHDR_SIZE = sizeof(PKHDR);
static const DWORD dwUlongSize = sizeof(ULONG);

struct time_conv
	{
	ULONGLONG reference;
	struct timeval start;
	};

typedef struct DEVICE_EXTENSION 
	{
	ULONG WhichObj;							// 0x00000000 : Filter device object, 0x00000001 : Control device object...
    ULONG signature;						// Memory signature of a device extension, for debugging.
    enum deviceState state;					// Plug-and-play state of this device object.
    PDEVICE_OBJECT filterDevObj;			// The device object that this filter driver created.
    PDEVICE_OBJECT physicalDevObj;			// The device object created by the next lower driver.
    PDEVICE_OBJECT topDevObj;				// The device object at the top of the stack that we attached to.
											// This is often (but not always) the same as physicalDevObj.
	PDEVICE_OBJECT controlDevObj;			// for control device object...
    
	DEVICE_CAPABILITIES deviceCapabilities;	// deviceCapabilities includes 
											// a table mapping system power states to device power states.
    
    LONG pendingActionCount;				// pendingActionCount is used to keep track of outstanding actions.
											// removeEvent is used to wait until all pending actions are
											// completed before complete the REMOVE_DEVICE IRP and let the
											// driver get unloaded.
    KEVENT removeEvent;

	enum capture readflag;					// for data capture...
	enum capture writeflag;

	PKEVENT packet_Event;					// for data capture event...
	PKEVENT removeEvent_app;				// for Notification when driver surprise remove...

	struct time_conv start_time;

	KSEMAPHORE	pSemaphore;
	PETHREAD	pThread;
	bool		bThreadStop;
	bool		bInit;

	/// for capture
	unsigned char*		pWriteBuffer[2];
	unsigned char*		pReadBuffer[2];
	unsigned char*		pCaptureBuffer;
	PKHDR_MERGE*		pCaptureHdr;

	DWORD		wWriteOffset[2];
	DWORD		wReadOffset[2];

	bool		bWhichBuffer_write;
	bool		bWhichBuffer_read;

	PKHDR		PkHdr[MAX_PACKET];
	int			nCount;
	int			nMergeCount;
	DWORD		dwMergeOffset;
	DWORD		dwTotalOffset;
	FAST_MUTEX	FMutex;

	/// for packet delay
	KTIMER		kTimer;
	KDPC		kTimerDpc;
	LARGE_INTEGER	lnTimer;
	KEVENT		kTimerEvent;	/// for packet blocking
	};

#if DBG
    #define DBGOUT(params_in_parentheses)   \
        {                                               \
            DbgPrint("'FILTER> "); \
            DbgPrint params_in_parentheses; \
            DbgPrint("\n"); \
        }
    #define TRAP(msg)  \
        {   \
            DBGOUT(("TRAP at file %s, line %d: '%s'.", __FILE__, __LINE__, msg)); \
            DbgBreakPoint(); \
        }
#else
    #define DBGOUT(params_in_parentheses)
    #define TRAP(msg)
#endif

#define BUFFERED_CTL_CODE(_x) \
		(ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, _x, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define DIRECT_CTL_CODE(_x) \
		(ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, _x, METHOD_IN_DIRECT, FILE_ANY_ACCESS)

#define NEITHER_CTL_CODE(_x) \
		(ULONG)CTL_CODE(FILE_DEVICE_UNKNOWN, _x, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_TEST_BUFFERED			BUFFERED_CTL_CODE(0x801)
#define IOCTL_CAPTURE_READ_DATA		DIRECT_CTL_CODE(0x802)
#define IOCTL_CAPTURE_WRITE_DATA	DIRECT_CTL_CODE(0x803)
#define IOCTL_CAPTURE_STOP			BUFFERED_CTL_CODE(0x804)
#define IOCTL_SET_EVENT				BUFFERED_CTL_CODE(0x805)
#define IOCTL_CAPTURE_DATA			DIRECT_CTL_CODE(0x806)
#define IOCTL_CAPTURE_START			BUFFERED_CTL_CODE(0x807)

__inline VOID TIME_SYNCHRONIZE(struct time_conv *data)
	{
	struct timeval tmp;
	LARGE_INTEGER SystemTime;
	LARGE_INTEGER TimeFreq,PTime;

	if (data->reference!=0)
		return;
	
	// get the absolute value of the system boot time.   
	
	PTime=KeQueryPerformanceCounter(&TimeFreq);
	KeQuerySystemTime(&SystemTime);
	
	tmp.tv_sec=(LONG)(SystemTime.QuadPart/10000000-11644473600);

	tmp.tv_usec=(LONG)((SystemTime.QuadPart%10000000)/10);

	tmp.tv_sec-=(ULONG)(PTime.QuadPart/TimeFreq.QuadPart);

	tmp.tv_usec-=(LONG)((PTime.QuadPart%TimeFreq.QuadPart)*1000000/TimeFreq.QuadPart);

	if (tmp.tv_usec<0)
		{
		tmp.tv_sec--;
		tmp.tv_usec+=1000000;
		}
	
	data->start=tmp;
	data->reference=1;
	}

__inline void GET_TIME(struct timeval *dst, struct time_conv *data)
	{
	LARGE_INTEGER PTime, TimeFreq;
	LONG tmp;

	PTime=KeQueryPerformanceCounter(&TimeFreq);
	tmp=(LONG)(PTime.QuadPart/TimeFreq.QuadPart);
	dst->tv_sec=data->start.tv_sec+tmp;
	dst->tv_usec=data->start.tv_usec+(LONG)((PTime.QuadPart%TimeFreq.QuadPart)*1000000/TimeFreq.QuadPart);
	
	if (dst->tv_usec>=1000000)
		{
		dst->tv_sec++;
		dst->tv_usec-=1000000;
		}

	}

__inline void TIME_DESYNCHRONIZE(struct time_conv *data)
	{
	data->reference = 0;
	data->start.tv_sec = 0;
	data->start.tv_usec = 0;
	}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath);
NTSTATUS USB_AddDevice(IN PDRIVER_OBJECT driverObj, IN PDEVICE_OBJECT pdo);
VOID     USB_DriverUnload(IN PDRIVER_OBJECT DriverObject);
NTSTATUS USB_Dispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS USB_PnP(struct DEVICE_EXTENSION *devExt, PIRP irp);
NTSTATUS USB_Read_Write(struct DEVICE_EXTENSION *devExt, PIRP irp);
NTSTATUS USB_IoReadCompletion(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PVOID Context);
NTSTATUS USB_IoCtrl(struct DEVICE_EXTENSION *devExt, PIRP irp);
NTSTATUS USB_Power(struct DEVICE_EXTENSION *devExt, PIRP irp);
NTSTATUS USB_PowerComplete(IN PDEVICE_OBJECT devObj, IN PIRP irp, IN PVOID context);
NTSTATUS GetDeviceCapabilities(struct DEVICE_EXTENSION *devExt);
NTSTATUS CallNextDriverSync(struct DEVICE_EXTENSION *devExt, PIRP irp);
NTSTATUS CallDriverSync(PDEVICE_OBJECT devObj, PIRP irp);
NTSTATUS CallDriverSyncCompletion(IN PDEVICE_OBJECT devObj, IN PIRP irp, IN PVOID Context);
VOID     IncrementPendingActionCount(struct DEVICE_EXTENSION *devExt);
VOID     DecrementPendingActionCount(struct DEVICE_EXTENSION *devExt);
NTSTATUS QueryDeviceKey(HANDLE Handle, PWCHAR ValueNameString, PVOID Data, ULONG DataLength);
VOID     RegistryAccessSample(struct DEVICE_EXTENSION *devExt, PDEVICE_OBJECT devObj);
VOID	 CaptureThread(IN PVOID Context);
VOID	 KillThread(struct DEVICE_EXTENSION *devExt);
NTSTATUS CaptureInit(struct DEVICE_EXTENSION *devExt, bool bInit);

VOID	 TimerDpc(IN PKDPC pDpc, IN PVOID pContext, IN PVOID SysArg1, IN PVOID SysArg2);

#endif