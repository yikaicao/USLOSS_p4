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
} /* end Sleep */

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
} /* end DiskSize */


/*
 *  Routine:  DiskRead
 *
 *  Description: This routin helps user-level processes to read one or more sectors from a disk.
 *
 *  Arguments:   As declared in the function.
 *
 *  Return Value: -1 if illegal values are given as input; 0 otherwise.
 *
 */
int DiskRead(void *dbuff, int unit, int track, int first, int sectors, int *status)
{
    systemArgs sysArg;
    CHECKMODE;
    
    sysArg.number   = SYS_DISKREAD;
    sysArg.arg1     = dbuff;
    sysArg.arg2     = (void *) ((long) sectors);
    sysArg.arg3     = (void *) ((long) track);
    sysArg.arg4     = (void *) ((long) first);
    sysArg.arg5     = (void *) ((long) unit);
    
    USLOSS_Syscall(&sysArg);
    
    *status = (long) sysArg.arg1;
    
    return (long) sysArg.arg4;
} /* end DiskRead */

/*
 *  Routine:  DiskWrite
 *
 *  Description: This routin helps user-level processes to write one or more sectors from a disk.
 *
 *  Arguments:   As declared in the function.
 *
 *  Return Value: -1 if illegal values are given as input; 0 otherwise.
 *
 */
int DiskWrite(void *dbuff, int unit, int track, int first, int sectors, int *status)
{
    systemArgs sysArg;
    CHECKMODE;
    
    sysArg.number   = SYS_DISKWRITE;
    sysArg.arg1     = dbuff;
    sysArg.arg2     = (void *) ((long) sectors);
    sysArg.arg3     = (void *) ((long) track);
    sysArg.arg4     = (void *) ((long) first);
    sysArg.arg5     = (void *) ((long) unit);
    
    USLOSS_Syscall(&sysArg);
    
    *status = (long) sysArg.arg1;
    
    return (long) sysArg.arg4;
} /* end DiskWrite */

/*
 *  Routine:  TermRead
 *
 *  Description: This routin helps user-level processes to read a line from a terminal
 *
 *  Arguments:   buf to store the line, maximum size of the buffer can be used, unit number of terminal, size of actual read line.
 *
 *  Return Value: -1 if illegal values are given as input; 0 otherwise.
 *
 */
int TermRead(char* buf, int size, int unit, int* sizeRead)
{
    systemArgs sysArg;
    CHECKMODE;
    
    sysArg.number   = SYS_TERMREAD;
    sysArg.arg1     = buf;
    sysArg.arg2     = (void *) ((long) size);
    sysArg.arg3     = (void *) ((long) unit);
    
    USLOSS_Syscall(&sysArg);
    
    *sizeRead = (long) sysArg.arg2;
    
    return (long) sysArg.arg4;
} /* end TermRead */
