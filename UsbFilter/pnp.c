#include <WDM.H>

#include "filter.h"


#ifdef ALLOC_PRAGMA
        #pragma alloc_text(PAGE, USB_PnP)
        #pragma alloc_text(PAGE, GetDeviceCapabilities)
#endif

static char* pnp_minor[] = {
		"IRP_MN_START_DEVICE",
		"IRP_MN_QUERY_REMOVE_DEVICE",
		"IRP_MN_REMOVE_DEVICE",
		"IRP_MN_CANCEL_REMOVE_DEVICE",
		"IRP_MN_STOP_DEVICE",
		"IRP_MN_QUERY_STOP_DEVICE",
		"IRP_MN_CANCEL_STOP_DEVICE",
		"IRP_MN_QUERY_DEVICE_RELATIONS",
		"IRP_MN_QUERY_INTERFACE",
		"IRP_MN_QUERY_CAPABILITIES",
		"IRP_MN_QUERY_RESOURCES",
		"IRP_MN_QUERY_RESOURCE_REQUIREMENTS",
		"IRP_MN_QUERY_DEVICE_TEXT",
		"IRP_MN_FILTER_RESOURCE_REQUIREMENTS",
		"Reserved",
		"IRP_MN_READ_CONFIG",
		"IRP_MN_WRITE_CONFIG",
		"IRP_MN_EJECT",
		"IRP_MN_SET_LOCK",
		"IRP_MN_QUERY_ID",
		"IRP_MN_QUERY_PNP_DEVICE_STATE",
		"IRP_MN_QUERY_BUS_INFORMATION",
		"IRP_MN_DEVICE_USAGE_NOTIFICATION",
		"IRP_MN_SURPRISE_REMOVAL",
	};

NTSTATUS USB_PnP(struct DEVICE_EXTENSION *devExt, PIRP irp)
	{
    PIO_STACK_LOCATION irpSp;
    NTSTATUS status = STATUS_SUCCESS;
    BOOLEAN completeIrpHere = FALSE;
    BOOLEAN justReturnStatus = FALSE;

    PAGED_CODE();

    irpSp = IoGetCurrentIrpStackLocation(irp);

//	if (irpSp->MinorFunction <= 0x17)
//		{
//		DBGOUT(("USB_PnP, minorFunc = %d (%s)", (ULONG)irpSp->MinorFunction, pnp_minor[irpSp->MinorFunction]));
//		}
//	else
//		{
//		DBGOUT(("USB_PnP, minorFunc = %d (Unkown minor function)", (ULONG)irpSp->MinorFunction));
//		}

    switch (irpSp->MinorFunction)
		{
	    case IRP_MN_START_DEVICE:
			{
//		    DBGOUT(("START_DEVICE"));
	        devExt->state = STATE_STARTING;
			IoCopyCurrentIrpStackLocationToNext(irp);
			status = CallNextDriverSync(devExt, irp);

			if (NT_SUCCESS(status))
				{
				status = GetDeviceCapabilities(devExt);
				if (NT_SUCCESS(status))
					{
					devExt->state = STATE_STARTED;
					}
				else 
					{
					devExt->state = STATE_START_FAILED;
					}
				}
			else 
				{
				devExt->state = STATE_START_FAILED;
				}
			completeIrpHere = TRUE;
			break;
			}

		case IRP_MN_QUERY_STOP_DEVICE:
		case IRP_MN_QUERY_REMOVE_DEVICE:
			{
			irp->IoStatus.Status = STATUS_SUCCESS;

			break;
			}

		case IRP_MN_STOP_DEVICE:
			{
			if (devExt->state == STATE_SUSPENDED)
				{
				status = STATUS_DEVICE_POWER_FAILURE;
				completeIrpHere = TRUE;
				}
			else 
				{
				if (devExt->state == STATE_STARTED)
					{
					devExt->state = STATE_STOPPED;
					}
				}

			break;
			}
			
		case IRP_MN_SURPRISE_REMOVAL:
			{
			DBGOUT(("SURPRISE_REMOVAL"));

			irp->IoStatus.Status = STATUS_SUCCESS;
			devExt->state = STATE_REMOVING;
			devExt->readflag = CAPTURE_STOP;
			devExt->writeflag = CAPTURE_STOP;
			if (devExt->removeEvent_app)
				KeSetEvent(devExt->removeEvent_app, 0, FALSE);

			break;
			}

		case IRP_MN_REMOVE_DEVICE:
			{
			DBGOUT(("REMOVE_DEVICE"));
			if (devExt->state != STATE_REMOVED)
				{
				devExt->state = STATE_REMOVED;

				IoCopyCurrentIrpStackLocationToNext(irp);
				status = IoCallDriver(devExt->topDevObj, irp);
				justReturnStatus = TRUE;

				DBGOUT(("REMOVE_DEVICE - waiting for %d irps to complete...",
						devExt->pendingActionCount));

				DecrementPendingActionCount(devExt);
				KeWaitForSingleObject(&devExt->removeEvent, Executive, KernelMode,	FALSE, NULL);

				DBGOUT(("REMOVE_DEVICE - ... DONE waiting. "));

				if (devExt->bInit)
					CaptureInit(devExt, false);
				
				IoDetachDevice(devExt->topDevObj);
				DBGOUT(("Detach Device..."));

				IoDeleteDevice(devExt->filterDevObj);
				DBGOUT(("Delete filterDevice"));

				IoDeleteDevice(devExt->controlDevObj);
				DBGOUT(("Delete cotrolDevice"));

				IoDeleteSymbolicLink(&DevlinkName);
				DBGOUT(("Delete Symbolic Link"));
				}
			break;
			}

	    case IRP_MN_QUERY_DEVICE_RELATIONS:
		default:
			break;


		}

    if (justReturnStatus)
		{
        /*
         *  We've already sent this IRP down the stack.
         */
		}
    else if (completeIrpHere)
		{
        irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
		}
    else 
		{
        IoCopyCurrentIrpStackLocationToNext(irp);
        status = IoCallDriver(devExt->topDevObj, irp);
		}

    return status;
	}

NTSTATUS GetDeviceCapabilities(struct DEVICE_EXTENSION *devExt)
	{
    NTSTATUS status;
    PIRP irp;

    PAGED_CODE();

    irp = IoAllocateIrp(devExt->topDevObj->StackSize, FALSE);
    if (irp)
		{
        PIO_STACK_LOCATION nextSp = IoGetNextIrpStackLocation(irp);

        // must initialize DeviceCapabilities before sending...
        RtlZeroMemory(&devExt->deviceCapabilities, sizeof(DEVICE_CAPABILITIES));
        devExt->deviceCapabilities.Size = sizeof(DEVICE_CAPABILITIES);
        devExt->deviceCapabilities.Version = 1;
        devExt->deviceCapabilities.Address = -1;
        devExt->deviceCapabilities.UINumber = -1;

        // setup irp stack location...
        nextSp->MajorFunction = IRP_MJ_PNP;
        nextSp->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
        nextSp->Parameters.DeviceCapabilities.Capabilities = &devExt->deviceCapabilities;

        /*
         *  For any IRP you create, you must set the default status
         *  to STATUS_NOT_SUPPORTED before sending it.
         */
        irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

        status = CallNextDriverSync(devExt, irp);

        IoFreeIrp(irp);
		}
    else
		{
        status = STATUS_INSUFFICIENT_RESOURCES;
		}

    ASSERT(NT_SUCCESS(status));
    return status;
	}