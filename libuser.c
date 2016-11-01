/*
 *  File:  libuser.c
 *
 *  Description:  This file contains the interface declarations
 *                to the OS kernel support package.
 *
 */

#include <phase1.h>
#include <phase2.h>
#include <libuser.h>
#include <usyscall.h>
#include <usloss.h>

#define CHECKMODE {    \
    if (USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) { \
        USLOSS_Console("Trying to invoke syscall from kernel\n"); \
        USLOSS_Halt(1);  \
    }  \
}

/*
 *  Routine:  Sleep
 *
 *  Description: This is the call entry to put a process into sleep.
 *
 *  Arguments:    int seconds    -- guaranteed sleep time
 *
 *  Return Value: 0 means success, -1 means error occurs
 *
 */
int Sleep(int seconds)
{
    systemArgs sysArg;
    CHECKMODE;
    
    sysArg.number = SYS_SLEEP;
    sysArg.arg1 = (void *) ((long) seconds);
    
    USLOSS_Syscall(&sysArg);
    
    return (long) sysArg.arg4;
}

/* end Sleep */

/*
 *  Routine:  DiskSize
 *
 *  Description: This routin returns information about the size of the disk (diskSize).
 *
 *  Arguments:    unit, how many bytes in one sector, how many sectors in one track, and how many tracks in one disk
 *
 *  Return Value: 0 means success, -1 means error occurs
 *
 */
int DiskSize(int unit, int* sectorSize, int* trackSize, int* diskSize)
{
    systemArgs sysArg;
    CHECKMODE;
    
    sysArg.number = SYS_DISKSIZE;
    sysArg.arg1 = (void *) ((long) unit);
    
    USLOSS_Syscall(&sysArg);
    
    *sectorSize = (long)sysArg.arg1;
    *trackSize  = (long)sysArg.arg2;
    *diskSize   = (long)sysArg.arg3;
    
    return (long) sysArg.arg4;
}

/* end DiskSize */
