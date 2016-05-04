#include <WDM.H>

#include "filter.h"


#ifdef ALLOC_PRAGMA
    #ifdef HANDLE_DEVICE_USAGE
        #pragma alloc_text(PAGEPOWR, VA_Power)
    #endif // HANDLE_DEVICE_USAGE
#endif

static char* power_minor[] = {
	"IRP_MN_WAIT_WAKE",
	"IRP_MN_POWER_SEQUENCE",
	"IRP_MN_SET_POWER",
	"IRP_MN_QUERY_POWER"
	};

NTSTATUS USB_Power(struct DEVICE_EXTENSION *devExt, PIRP irp)
	{
    PIO_STACK_LOCATION irpSp;
    NTSTATUS status;

    irpSp = IoGetCurrentIrpStackLocation(irp);

//	if (irpSp->MinorFunction <= 0x03)
//		{
//	    DBGOUT(("USB_Power, minorFunc = %d (%s)", (ULONG)irpSp->MinorFunction, power_minor[irpSp->MinorFunction]));
//		}
//	else
//		{
//		DBGOUT(("USB_Power, minorFunc = %d (Unknown minor function)", (ULONG)irpSp->MinorFunction));
//		}

    switch (irpSp->MinorFunction)
		{
        case IRP_MN_SET_POWER:
			{
            switch (irpSp->Parameters.Power.Type) 
				{
                case SystemPowerState:
                    /*
                     *  For system power states, just pass the IRP down.
                     */
                    break;

                case DevicePowerState:
					{
                    switch (irpSp->Parameters.Power.State.DeviceState) 
						{
                        case PowerDeviceD0:
							{
                            /*
                             *  Resume from APM Suspend
                             *
                             *  Do nothing here; 
                             *  Send down the read IRPs in the completion
                             *  routine for this (the power) IRP.
                             */
                            break;
							}
                        case PowerDeviceD1:
                        case PowerDeviceD2:
                        case PowerDeviceD3:
							{
                            /*
                             *  Suspend
                             */
                            if (devExt->state == STATE_STARTED)
								{
                                devExt->state = STATE_SUSPENDED;
								}
                            break;
							}
						}
                    break;
					}
				}
            break;
			}
		}


    /*
     *  Send the IRP down the driver stack,
     *  using PoCallDriver (not IoCallDriver, as for non-power irps).
     */
    IncrementPendingActionCount(devExt);
    IoCopyCurrentIrpStackLocationToNext(irp);
    IoSetCompletionRoutine(irp, USB_PowerComplete, (PVOID)devExt, TRUE, TRUE, TRUE);
    status = PoCallDriver(devExt->topDevObj, irp);

    return status;
	}


NTSTATUS USB_PowerComplete(IN PDEVICE_OBJECT devObj, IN PIRP irp, IN PVOID context)
	{
    PIO_STACK_LOCATION irpSp;
    struct DEVICE_EXTENSION *devExt = (struct DEVICE_EXTENSION *)context;

    ASSERT(devExt);
    ASSERT(devExt->signature == DEVICE_EXTENSION_SIGNATURE); 

    /*
     *  If the lower driver returned PENDING, mark our stack location as
     *  pending also.
     */
    if (irp->PendingReturned)
		{
        IoMarkIrpPending(irp);
		}

    irpSp = IoGetCurrentIrpStackLocation(irp);
    ASSERT(irpSp->MajorFunction == IRP_MJ_POWER);

    if (NT_SUCCESS(irp->IoStatus.Status))
		{
        switch (irpSp->MinorFunction)
			{
            case IRP_MN_SET_POWER:
				{
                switch (irpSp->Parameters.Power.Type)
					{
                    case DevicePowerState:
						{
                        switch (irpSp->Parameters.Power.State.DeviceState)
							{
                            case PowerDeviceD0:
								{
                                if (devExt->state == STATE_SUSPENDED)
									{
                                    devExt->state = STATE_STARTED;
									}
                                break;
								}
							}
                        break;
						}
					}
                break;
				}
			}
		}
    
    
    /*
     *  Whether we are completing or relaying this power IRP,
     *  we must call PoStartNextPowerIrp.
     */
    PoStartNextPowerIrp(irp);

    /*
     *  Decrement the pendingActionCount, which we incremented in VA_Power.
     */
    DecrementPendingActionCount(devExt);

    return STATUS_SUCCESS;
	}




