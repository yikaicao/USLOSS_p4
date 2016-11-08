#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <stdlib.h> /* needed for atoi() */
#include <stdio.h>
#include <libuser.h>
#include <providedPrototypes.h>

// global structures
int debugflag4 = 0;
int diskDebug = 0;
int semRunning;
procStruct ProcTable[MAXPROC];
procPtr sleepList;

// disk structures
int diskFinishFlag[USLOSS_DISK_UNITS];
int diskTrack[USLOSS_DISK_UNITS]; // contains number of tracks per disk unit
int diskMbox[USLOSS_DISK_UNITS];
procPtr diskQueue[USLOSS_DISK_UNITS];

// term structures
int lineBuffered[USLOSS_TERM_UNITS];
int termDriverPID[USLOSS_TERM_UNITS];
int termReaderPID[USLOSS_TERM_UNITS];
int charinMbox[USLOSS_TERM_UNITS]; // to transfer status register
int termReaderMbox[USLOSS_TERM_UNITS]; // to transfer one buffered line to termReadReal
int termWriterPID[USLOSS_TERM_UNITS];
int charoutMbox[USLOSS_TERM_UNITS]; // to transfer status register
int termWriterPIDMbox[USLOSS_TERM_UNITS]; // to keep track of which user process is blocked
int termWriterLineMbox[USLOSS_TERM_UNITS]; // to transfer the line to be written
int termWriteSem; // only allow one process to write on terminal device at the same time

// driver processes
static int ClockDriver(char *);
static int DiskDriver(char *);
static int TermDriver(char *);
static int TermReader(char *);
static int TermWriter(char *);

// system helpers
void sleep(systemArgs *);
int sleepReal(int);
void diskSize(systemArgs *);
int diskSizeReal(int, int*, int*, int*);
void diskWrite(systemArgs *);
int diskWriteReal(char*, int, int, int, int, int*);
int diskRequest(char*, int, int, int, int);
void diskRead(systemArgs *);
int diskReadReal(char*, int, int, int, int, int*);
void termRead(systemArgs *);
int termReadReal(char*, int, int, int*);
void termWrite(systemArgs *);
int termWriteReal(char*, int, int, int*);

// kernel helpers
void check_kernel_mode(char *);
void setUserMode();
void initSysCallVec();
void initProcTable();
void clearProcess(int);
void printProcTable();
void addSleepRequest(procPtr*, procPtr);
void printSleepList();
void addDiskRequest(procPtr*, procPtr);
void printDiskReqQueue(procPtr*);
void dequeueDiskReq(procPtr*);

void start3(void)
{
    char    buf[128]; // buffer for startarg
    int     i;
    int     clockPID;
    int     pid;
    int     status;
    
    /*
     * Check kernel mode here.
     */
    check_kernel_mode("start3");
    
    /*
     * Initialize phase4 supportive structures
     */
    initSysCallVec();
    initProcTable();
    
    
    
    /*
     * Create clock device driver
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    semRunning = semcreateReal(0);
    sleepList = NULL;
    clockPID = fork1("Clock driver", ClockDriver, NULL, USLOSS_MIN_STACK, 2);
    if (clockPID < 0) {
        USLOSS_Console("start3(): Can't create clock driver\n");
        USLOSS_Halt(1);
    }
    /*
     * Wait for the clock driver to start. The idea is that ClockDriver
     * will V the semaphore "semRunning" once it is running.
     */
    sempReal(semRunning);
    /* --------------------------------------------ClockDriver created */
    
    
    
    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */
    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
        sprintf(buf, "%d", i);
        pid = fork1("Disk driver", DiskDriver, buf, USLOSS_MIN_STACK, 2);
        diskQueue[i] = NULL;
        diskFinishFlag[i] = 0;
        if (pid < 0) {
            USLOSS_Console("start3(): Can't create term driver %d\n", i);
            USLOSS_Halt(1);
        }
    }
    sempReal(semRunning);
    sempReal(semRunning);
    /* --------------------------------------------DiskDriver(s) created */
    
    /*
     * Create terminal device drivers.
     */
    termWriteSem = semcreateReal(1); // only allow one process to write at the same time
    for (i = 0; i < USLOSS_TERM_UNITS; i++)
    {
        sprintf(buf, "%d", i);
        termDriverPID[i] = fork1("Term driver", TermDriver, buf, USLOSS_MIN_STACK, 2);
        lineBuffered[i] = 0;
        charinMbox[i] = MboxCreate(0, sizeof(int));
        
        if (debugflag4)
            USLOSS_Console("\tcreated mail box for term unit %d, with id of %d\n", i, charinMbox[i]);
        
        termReaderMbox[i] = MboxCreate(10, sizeof(char) * MAXLINE + 1);
        termReaderPID[i] = fork1("Term reader", TermReader, buf, USLOSS_MIN_STACK, 2);
        charoutMbox[i] = MboxCreate(0, sizeof(int));
        termWriterPID[i] = fork1("Term writer", TermWriter, buf, USLOSS_MIN_STACK, 2);
        termWriterPIDMbox[i] = MboxCreate(1, sizeof(int));
        termWriterLineMbox[i] = MboxCreate(10, sizeof(char) * MAXLINE + 1);
        sempReal(semRunning);
        sempReal(semRunning);
        sempReal(semRunning);
    }
    /* --------------------------------------------TerminalDriver(s) created */
    
    
    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * I'm assuming kernel-mode versions of the system calls
     * with lower-case first letters.
     */
    pid = spawnReal("start4", start4, NULL, 4 * USLOSS_MIN_STACK, 3);
    
    pid = waitReal(&status);
    

    
    /*
     * Zap the device drivers
     */
    zap(clockPID);  // clock driver
    join(&status);
    
    // quit disk drivers
    for (i = 0; i < USLOSS_DISK_UNITS; i++)
    {
        diskFinishFlag[i] = 1;
        MboxSend(diskMbox[i], 0, 0);
        join(&status);
    }
    
    // quit term drivers
    char filename[20]; // a buffer to store terminal file names
    for (i = 0; i < USLOSS_TERM_UNITS; i++)
    {
        // wake up term driver one more time
        int ctrl = 0;
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, i, (void *)((long) ctrl));
        
        // update the terminal file in order to quit USLOSS
        sprintf(filename, "term%d.in", i);
        FILE *f = fopen(filename, "a+");
        fprintf(f, "End of this simulation\n");
        fflush(f);
        fclose(f);
        
        // zap term driver and join it
        zap(termDriverPID[i]);
        join(&status);
        
        // zap term reader and join it
        MboxSend(charinMbox[i], NULL, 0);
        zap(termReaderPID[i]);
        join(&status);
        
        // zap term writer and join it
        MboxSend(termWriterLineMbox[i], NULL, 0);
        zap(termWriterPID[i]);
        join(&status);
    }
    /* end of zapping device drivers */
    
    // eventually, at the end:
    quit(0);
    
} /* end of start3 */








//%%%%%%%%%%%%%%%%%%%%%%%%% driver processes %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/* ------------------------- ClockDriver ----------------------------------- */
static int ClockDriver(char *arg)
{
    if (debugflag4)
        USLOSS_Console("ClockDriver(): entered\n");
    
    int result;
    int status;
    
    // Let the parent know we are running and enable interrupts.
    semvReal(semRunning);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
    
    // Infinite loop until we are zap'd
    while(! isZapped()) {
        result = waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        if (result != 0) {
            return 0;
        }
        /*
         * Compute the current time and wake up any processes
         * whose time has come.
         */
        while (sleepList != NULL && sleepList->wakeTime < USLOSS_Clock())
        {
            MboxCondSend(sleepList->privateMboxID, 0, 0);
            sleepList = sleepList->nextSleepPtr;
        }
    }
    
    procPtr proc = sleepList;
    while(proc != NULL){
        // Send to free a process
        MboxSend(proc->privateMboxID, 0, 0);
    }
    
    return 0;
} /* end of ClockDriver */

/* ------------------------- DiskDriver ----------------------------------- */
static int DiskDriver(char *arg)
{
    /*
     * Initialization of disk driver
     */
    int unit = atoi( (char *) arg); 	// Unit is passed as arg.
    int status;
    
    // update size of disk track
    USLOSS_DeviceRequest req = (USLOSS_DeviceRequest){
        .opr = USLOSS_DISK_TRACKS,
        .reg1 = &diskTrack[unit],
    };
    
    // request our device request
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
    waitDevice(USLOSS_DISK_DEV, unit, &status); // wait for request to be completed
    
    // create disk driver's private mail box
    diskMbox[unit] = MboxCreate(MAXPROC, 0);
    
    // Let the parent know we are running and enable interrupts.
    semvReal(semRunning);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
    /* end of initialization */
    
    // begin its service
    while (!isZapped())
    {
        MboxReceive(diskMbox[unit], NULL, 0);
        
        // quit DiskDriver when start4 is finished
        if (diskFinishFlag[unit])
        {
            if (debugflag4)
                USLOSS_Console("DiskDriver(): no more request, disk %d quitting.\n", unit);
            break;
        }
        
        
        // get the head request
        procPtr headReq = diskQueue[unit];
        
        if (debugflag4 || diskDebug)
            USLOSS_Console("DiskDriver(): disk %d woke up\n\t going to %s track %d requested by process %d\n", unit, headReq->opr == USLOSS_DISK_WRITE ? "write" : "read", headReq->track, headReq->pid);
        
        
        // move to the right track
        USLOSS_DeviceRequest req;
        req.opr = USLOSS_DISK_SEEK;
        req.reg1 = (void*)(long)headReq->track;
        USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
        waitDevice(USLOSS_DISK_DEV, unit, &status);
        
        // start to read or write
        int sectorCounter = headReq->sectors;
        int currSector = headReq->first;
        int currTrack = headReq->track;
        char* buf = headReq->buf;
        while (sectorCounter > 0)
        {
            req.opr = headReq->opr;
            req.reg1 = (void*)(long)currSector;
            req.reg2 = (void*)(long)buf;
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
            waitDevice(USLOSS_DISK_DEV, unit, &status);
            
            currSector++;
            
            // track wrap around
            if(currSector >= diskTrack[unit]){
                if (debugflag4)
                    USLOSS_Console("DiskDriver(): wrapped around\n");
                currSector = 0;
                currTrack = (currTrack + 1) % diskTrack[unit];
                
                // move to next track
                req.opr = USLOSS_DISK_SEEK;
                req.reg1 = (void*)(long)currTrack;
                USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);
                waitDevice(USLOSS_DISK_DEV, unit, &status);
            }
            
            buf += USLOSS_DISK_SECTOR_SIZE;
            
            sectorCounter--;
        }
        
        if (debugflag4 || diskDebug)
            USLOSS_Console("DiskDriver(): request on track %d by process %d completed\n", headReq->track, headReq->pid);
        
        // move headReq to next request
        dequeueDiskReq(&diskQueue[unit]);
        
        if (debugflag4 || diskDebug)
        {
            USLOSS_Console("\tafter dequeue, new list is\n");
            printDiskReqQueue(&diskQueue[unit]);
        }
        
        // unblock waiting user-level process
        MboxSend(headReq->privateMboxID, NULL, 0);
        
    }
    
    return unit;
} /* end of DiskDriver */

/* ------------------------- TermDriver ----------------------------------- */
static int TermDriver(char *arg)
{
    int unit = atoi((char *) arg);
    int status;
    int result;
    
    
    semvReal(semRunning);
    // end of initialization
    
    // begin its service
    while (!isZapped())
    {
        result = waitDevice(USLOSS_TERM_DEV, unit, &status);
        
        if (debugflag4)
            USLOSS_Console("\tTermDriver(): woke up.\n");
        
        if (result != 0)
            return 0;
        
        // unblocked by TermRead
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY)
        {
            if (debugflag4)
                USLOSS_Console("\tTermDriver(): device %d received status of dev busy\n", unit);
            
            MboxSend(charinMbox[unit], &status, sizeof(int));
            
            if (debugflag4)
                USLOSS_Console("\tTermDriver(): sent a status reg to mailbox %d\n", charinMbox[unit]);
        }
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY)
        {
            MboxCondSend(charoutMbox[unit], &status, sizeof(int));
        }
    }
    
    return 0;
} /* end of TermDriver */

/* ------------------------- TermReader ----------------------------------- */
static int TermReader(char *arg)
{
    int unit = atoi((char *) arg);
    
    int statReg;
    char curLine[MAXLINE + 1];
    int curLinePos = 0;
    
    int i;
    for (i = 0; i < MAXLINE + 1; i++)
    {
        curLine[i] = '\0';
    }
    
    semvReal(semRunning);
    // end of initialization
    
    // begin its service
    while (!isZapped())
    {
        MboxReceive(charinMbox[unit], &statReg, sizeof(int));
        
        // get char received
        char received = USLOSS_TERM_STAT_CHAR(statReg);
        
        if (debugflag4)
            USLOSS_Console("\t\tTermReader(): unit %d received character '%c'\n", unit, received);
        
        // reaches MAXLINE
        if (curLinePos == MAXLINE)
        {
            // finish up the current line
            curLine[curLinePos] = '\n';
            
            if (debugflag4)
                USLOSS_Console("\t\tTermReader(): unit %d sending an incomplete line\n", unit);
            lineBuffered[unit]++;
            MboxCondSend(termReaderMbox[unit], &curLine, MAXLINE);
            
            // wipe out current line to get ready for next line
            curLinePos = 0;
            for (i = 0; i < MAXLINE + 1; i++){
                curLine[i] = '\0';
            }
            
            // put received char to a new line
            curLine[curLinePos] = received;
            curLinePos++;
        }
        // reaches a newline
        else if (received == '\n')
        {
            // finish up the current line
            curLine[curLinePos] = received;
            
            if (debugflag4)
                USLOSS_Console("\t\tTermReader(): unit %d sending a complete line\n", unit);
            lineBuffered[unit]++;
            MboxCondSend(termReaderMbox[unit], &curLine, MAXLINE);
            
            // wipe out current line to get ready for next line
            curLinePos = 0;
            for (i = 0; i < MAXLINE + 1; i++){
                curLine[i] = '\0';
            }
            
            // put received char to a new line
            curLine[curLinePos] = received;
            curLinePos++;
        }
        // normal cases
        else
        {
            curLine[curLinePos] = received;
            curLinePos++;
        }
    }
    
    return 0;
} /* end of TermReader */

/* ------------------------- TermWriter ----------------------------------- */
static int TermWriter(char *arg)
{
    int unit = atoi((char *) arg);
    
    char result[MAXLINE + 1];
    semvReal(semRunning);
    // end of initialization
    
    // begin its service
    while (!isZapped())
    {
        // get the line to be written
        int sizeToWrite = MboxReceive(termWriterLineMbox[unit], result, MAXLINE + 1);
        
        if (isZapped())
        {
            if (debugflag4)
                USLOSS_Console("\tTermWriter(): got fakePID, indicating I shall quit.\n");
            break;
        }
        
        
        char writeLine[MAXLINE + 1];
        
        if (debugflag4)
            USLOSS_Console("\tTermWriter(): going to write line size %d:\n\t\t%s", sizeToWrite, result);
        
        // turn on transmit bit
        int ctrl = 0;
        ctrl = USLOSS_TERM_CTRL_XMIT_INT((long)ctrl);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*) ((long)ctrl));
        
        // write line char by char
        int i = 0;
        int status = 0;
        while (sizeToWrite > i)
        {
            MboxReceive(charoutMbox[unit], &status, sizeof(int));
            
            //debug
            //USLOSS_Console("%c", writeLine[i]);
            
            ctrl = 0;
            ctrl =  USLOSS_TERM_CTRL_CHAR(ctrl, result[i]);
            ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
            ctrl =  USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
            
            USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*) ((long)ctrl));
            
            i++;
            
        }
        
        // read the last char
        //MboxReceive(charoutMbox[unit], result, sizeof(result));
        ctrl = 0;
        ctrl =  USLOSS_TERM_CTRL_CHAR(ctrl, result[i]);
        ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
        ctrl =  USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*) ((long)ctrl));
        
        // get which process to unblock
        int pid = -1;
        MboxReceive(termWriterPIDMbox[unit], &pid, sizeof(int));
        
        // unblock invoking process
        MboxSend(ProcTable[pid % MAXPROC].privateMboxID, &sizeToWrite, sizeof(int));
        
    }
    
    return 0;
} /* end of TermWriter */


















//%%%%%%%%%%%%%%%%%%%%%%%%% system helpers %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/* ------------------------- sleep ----------------------------------- */
void sleep(systemArgs *sysArg)
{
    if (debugflag4)
        USLOSS_Console("sleep(): entered\n");
    int seconds = (long) sysArg->arg1;
    
    sysArg->arg4 = (void *)((long)sleepReal(seconds));
    
    setUserMode();
} /* end of sleep */

/* ------------------------- sleepReal ----------------------------------- */
int sleepReal(int seconds)
{
    if (debugflag4)
        USLOSS_Console("sleepReal(): seconds = %d\n", seconds);
    
    long wakeTime = USLOSS_Clock() + (1000000 * seconds); // in microsecond
    
    // construct newSleep process
    procPtr newSleep = &ProcTable[getpid() % MAXPROC];
    newSleep->nextSleepPtr = NULL;
    newSleep->wakeTime = wakeTime;
    newSleep->pid = getpid();
    
    // put request on queue
    addSleepRequest(&sleepList, newSleep);
    
    // put process to sleep
    MboxReceive(newSleep->privateMboxID, 0, 0);
    
    
    return 0;
} /* end of sleepReal */

/* ------------------------- diskSize ----------------------------------- */
void diskSize(systemArgs *sysArg)
{
    if (debugflag4)
        USLOSS_Console("diskSize(): entered\n");
    
    int unit = (long) sysArg->arg1;
    
    int sector, track, disk;
    
    // update sysArg
    sysArg->arg4 = (void *)((long)diskSizeReal(unit, &sector, &track, &disk));
    sysArg->arg1 = (void *) ((long)sector);
    sysArg->arg2 = (void *) ((long)track);
    sysArg->arg3 = (void *) ((long)disk);
    
    if (debugflag4)
        USLOSS_Console("diskSize(): updated: unit %d, sector %d, track %d, disk %d\n", unit, sector, track, disk);
    
    setUserMode();
} /* end of diskSize */

/* ------------------------- diskSizeReal ----------------------------------- */
int diskSizeReal(int unit, int* sector, int* track, int* disk)
{
    if (unit < 0 || unit >= USLOSS_DISK_UNITS)
        return -1;
    
    *sector = USLOSS_DISK_SECTOR_SIZE;
    *track = USLOSS_DISK_TRACK_SIZE;
    *disk = diskTrack[unit];
    
    return 0;
    
} /* end of diskSizeReal */

/* ------------------------- diskWrite ----------------------------------- */
void diskWrite(systemArgs *sysArg)
{
    char* writeBuf = sysArg->arg1;
    int sectors = (long)sysArg->arg2;
    int track   = (long)sysArg->arg3;
    int first   = (long)sysArg->arg4;
    int unit    = (long)sysArg->arg5;
    
    if (debugflag4)
        USLOSS_Console("diskWrite(): writing: \n\t %s\n\t on unit %d, starting with track %d sector %d for %d sector(s)\n", writeBuf, unit, track, first, sectors);
    
    int status = 0; // 0 if transfer was sucessful; the disk status register otherwise
    int writeResult = diskWriteReal(writeBuf, sectors, track, first, unit, &status);
    
    sysArg->arg1 = (void *) ((long)status);
    sysArg->arg4 = (void *) ((long)writeResult);
    
} /* end of diskWrite */

/* ------------------------- diskWriteReal ----------------------------------- */
// purpose: call diskRequest to put new disk request on queue, wake up DiskDriver before blocking whichever user-level process that calls DiskWrite and wait till DiskDriver to finish this request
int diskWriteReal(char* writeBuf, int sectors, int track, int first, int unit, int* status)
{
    // handle illegal input
    if (unit < 0 || unit >= USLOSS_DISK_UNITS)
        return -1;
    if (sectors < 0 || track < 0 || track >= diskTrack[unit] || first >= USLOSS_DISK_TRACK_SIZE)
        return -1;
    
    
    
    // put request on queue
    ProcTable[getpid() % MAXPROC].opr = USLOSS_DISK_WRITE;
    diskRequest(writeBuf, sectors, track, first, unit);
    
    if (debugflag4 || diskDebug)
    {
        USLOSS_Console("\tdiskWriteReal(): after process %d's request received\n", getpid());
        printDiskReqQueue(&diskQueue[unit]);
        USLOSS_Console("\tdiskWriteReal(): process %d unblocking DeviceDriver %d\n", getpid(), unit);
    }
    
    // wake up disk driver
    MboxSend(diskMbox[unit], NULL, 0);
    
    if (debugflag4 || diskDebug)
        USLOSS_Console("\tdiskWriteReal(): blocking pid %d\n", getpid());
    
    // block current running user-level process
    MboxReceive(ProcTable[getpid() % MAXPROC].privateMboxID, 0, 0);
    
    if (debugflag4 || diskDebug)
        USLOSS_Console("\tdiskWriteReal(): process %d's request on track %d finished\n", getpid(), track);
    
    return 0;
} /* end of diskWriteReal */

/* ------------------------- diskRequest ----------------------------------- */
// purpose: log new disk request process in procTable4, call addDiskRequest to put new request on disk's queue
int diskRequest(char* buf, int sectors, int track, int first, int unit)
{
    if (debugflag4)
        USLOSS_Console("diskRequest(): enetered\n");
    
    int status = 0;
    
    // construct new disk request process
    procPtr newDisk = &ProcTable[getpid() % MAXPROC];
    newDisk->nextDiskPtr    = NULL;
    newDisk->pid            = getpid();
    newDisk->buf            = buf;
    newDisk->sectors        = sectors;
    newDisk->track          = track;
    newDisk->first          = first;
    newDisk->unit           = unit;
    
    // put request on queue
    addDiskRequest(&diskQueue[unit], newDisk);
    
    return status;
} /* end of diskRequest */

/* ------------------------- diskRead ----------------------------------- */
void diskRead(systemArgs *sysArg)
{
    char* readBuf = sysArg->arg1;
    int sectors = (long)sysArg->arg2;
    int track   = (long)sysArg->arg3;
    int first   = (long)sysArg->arg4;
    int unit    = (long)sysArg->arg5;
    
    if (debugflag4)
        USLOSS_Console("diskRead(): reading on unit %d, starting with track %d sector %d for %d sector(s)\n", unit, track, first, sectors);
    
    int status = 0; // 0 if transfer was sucessful; the disk status register otherwise
    int readResult = diskReadReal(readBuf, sectors, track, first, unit, &status);
    
    sysArg->arg1 = (void *) ((long)status);
    sysArg->arg4 = (void *) ((long)readResult);
    
} /* end of diskRead */

/* ------------------------- diskReadReal ----------------------------------- */
int diskReadReal(char* readBuf, int sectors, int track, int first, int unit, int* status)
{
    // handle illegal input
    if (unit < 0 || unit >= USLOSS_DISK_UNITS)
        return -1;
    if (sectors < 0 || track < 0 || track >= USLOSS_DISK_TRACK_SIZE)
        return -1;
    
    // put request on queue
    ProcTable[getpid() % MAXPROC].opr = USLOSS_DISK_READ;
    *status = diskRequest(readBuf, sectors, track, first, unit);
    
    // wake up disk driver
    MboxSend(diskMbox[unit], NULL, 0);
    
    // block current running user-level process
    MboxReceive(ProcTable[getpid() % MAXPROC].privateMboxID, 0, 0);
    
    return 0;
} /* end of diskWriteReal */

/* ------------------------- termRead ----------------------------------- */
void termRead(systemArgs* sysArg)
{
    if (debugflag4)
        USLOSS_Console("termRead(): entered\n");
    
    
    char* buf = (char *) sysArg->arg1;
    int size = (long) sysArg->arg2;
    int unit = (long) sysArg->arg3;
    
    int sizeRead = 0;
    int termResult = termReadReal(buf, size, unit, &sizeRead);
    
    sysArg->arg2 = (void *) ((long)sizeRead);
    sysArg->arg4 = (void *) ((long)termResult);
    
} /* end of termRead */

/* ------------------------- termReadReal ----------------------------------- */
int termReadReal(char* buf, int size, int unit, int* sizeRead)
{
    // check illegal input values
    if (size < 0 || size > MAXLINE || unit < 0 || unit >= USLOSS_TERM_UNITS)
        return -1;
    
    // storage place to receive from TermReader
    char recBuf[MAXLINE];
    
    // wake up TermDriver
    int ctrl = 0;
    ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)((long)ctrl));
    
    // block on TermReaderMbox
    MboxReceive(termReaderMbox[unit], recBuf, MAXLINE);
    
    if (debugflag4)
        USLOSS_Console("\ttermReadReal(): unit %d received a line:\n\t\t%s", unit, recBuf);
    
    // decrement line buffered
    lineBuffered[unit]--;
    
    // transfer received line to char* buf char by char
    int i = 0;
    while (recBuf[i] != '\0' && i < size)
    {
        buf[i] = recBuf[i];
        i++;
    }
    *sizeRead = i;
    
    return 0;
}

/* ------------------------- termWrite ----------------------------------- */
void termWrite(systemArgs* sysArg)
{
    char* buf = (char*) sysArg->arg1;
    int size = (long) sysArg->arg2;
    int unit = (long) sysArg->arg3;
    
    int sizeWritten = 0;
    int termResult = termWriteReal(buf, size, unit, &sizeWritten);
    
    sysArg->arg2 = (void *) ((long)sizeWritten);
    sysArg->arg4 = (void *) ((long)termResult);
} /* end of termWrite */

/* ------------------------- termWriteReal ----------------------------------- */
int termWriteReal(char* buf, int size, int unit, int* sizeWritten)
{
    // check illegal input values
    if (size < 0 || size > MAXLINE || unit < 0 || unit >= USLOSS_TERM_UNITS)
        return -1;
    
    // get to know this process
    int pid = getpid();
    ProcTable[pid % MAXPROC].pid = pid;
    
    if (debugflag4)
        USLOSS_Console("\t\ttermWriteReal(): user process %d wants to write on term %d with the following message:\n\t\t%s\n\t\t\tIt will be blocked on its private mailbox %d.\n", pid, unit, buf, ProcTable[pid % MAXPROC].privateMboxID);
    
    // inform TermWriter the invoking process pid
    MboxSend(termWriterPIDMbox[unit], &pid, sizeof(pid));
    
    // transfer the line that is going to be written
    MboxSend(termWriterLineMbox[unit], buf, size);
    
    // block the invoking process
    MboxReceive(ProcTable[pid % MAXPROC].privateMboxID, sizeWritten, sizeof(int));
    
    //debug
    //*sizeWritten = size;
    
    return 0;
}
















//%%%%%%%%%%%%%%%%%%%%%%%%% kernel helpers %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/* ------------------------- check_kernel_mode ----------------------------------- */
void check_kernel_mode(char *arg)
{
    if (!(USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()))
    {
        USLOSS_Console("%s(): called while in user mode. Halting...\n", arg);
    }
} /* end of check_kernel_mode */

/*---------- setUserMode ----------*/
void setUserMode()
{
    if(debugflag4)
        USLOSS_Console("setUserMode(): entered\n");
    
    USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE );
} /* setUserMode */

/*---------- initSysCallVec ----------*/
void initSysCallVec()
{
    if (debugflag4)
        USLOSS_Console("initSysCallVec(): entered");
    
    // known vectors
    systemCallVec[SYS_SLEEP] = (void *)sleep;
    systemCallVec[SYS_DISKSIZE] = (void *)diskSize;
    systemCallVec[SYS_DISKWRITE] = (void *)diskWrite;
    systemCallVec[SYS_DISKREAD] = (void *)diskRead;
    systemCallVec[SYS_TERMREAD] = (void *)termRead;
    systemCallVec[SYS_TERMWRITE] = (void *)termWrite;
    
} /* end of initSysCallVec */

/*---------- initProcTable ----------*/
void initProcTable()
{
    if (debugflag4)
        USLOSS_Console("initProcTable(): entered");
    
    int i;
    for (i = 0; i < MAXPROC; i++)
    {
        clearProcess(i);
    }
} /* end of initProcTable */

/* ------------------------- clearProcess ------------------------- */
void clearProcess(int pid)
{
    ProcTable[pid] = (procStruct) {
        .pid            = -1,
        .nextSleepPtr    = NULL,
        .privateMboxID  = MboxCreate(0,MAX_MESSAGE),
        .wakeTime       = 0
    };
    
} /* end of clearProcess */

/* ------------------------- printProcTable ------------------------- */
void printProcTable()
{
    int i;
    for (i = 0; i < MAXPROC; i++)
    {
        procStruct tmp = ProcTable[i];
        USLOSS_Console("pid %5d privateMboxID %d wakeTime %d\n", tmp.pid, tmp.privateMboxID, tmp.wakeTime);
    }
} /* printProcTable */

/* ------------------------- addSleepRequest ----------------------------------- */
void addSleepRequest(procPtr* list, procPtr newSleep)
{
    if(debugflag4)
        USLOSS_Console("addSleepRequest(): pid %d, wakeTime %d\n", newSleep->pid, newSleep->wakeTime);
    
    // add to empty list
    if (*list == NULL)
    {
        *list = newSleep;
        return;
    }
    // add to the head
    procPtr tmp = *list;
    if(tmp->wakeTime > newSleep->wakeTime)
    {
        newSleep->nextSleepPtr = *list;
        *list = newSleep;
        return;
    }
    // normal cases
    procPtr prev = NULL;
    procPtr curr = *list;
    while(curr->wakeTime < newSleep->wakeTime)
    {
        prev = curr;
        curr = curr->nextSleepPtr;
        if(curr == NULL)
            break;
    }
    prev->nextSleepPtr = newSleep;
    newSleep->nextSleepPtr = curr;
    
    return;
} /* end of addSleepRequest */

/* ------------------------- printSleepList ----------------------------------- */
void printSleepList()
{
    procPtr tmp = sleepList;
    while(tmp != NULL)
    {
        USLOSS_Console("\t printSleepList(): %d blocked on %d wakeTime %d\n", tmp->pid, tmp->privateMboxID, tmp->wakeTime);
        tmp = tmp->nextSleepPtr;
    }
} /* end of printSleepList */

/* ------------------------- addDiskRequest ----------------------------------- */
// similar with addSleepList, same algorithm, be careful with the track directions
void addDiskRequest(procPtr* diskReqQueue, procPtr newDisk)
{
    if (debugflag4)
        USLOSS_Console("addDiskRequest(): inserting request for track #%d\n", newDisk->track);
    
    procPtr head = *diskReqQueue;
    if (*diskReqQueue == NULL)
    {
        *diskReqQueue = newDisk;
        return;
    }
    // adding when track is to the left of current
    else if (head->track < newDisk->track)
    {
        procPtr prev = NULL;
        procPtr tmp = *diskReqQueue;
        
        while (tmp->track < newDisk->track)
        {
            if (tmp->track < head->track)
            {
                prev->nextDiskPtr = newDisk;
                newDisk->nextDiskPtr = tmp;
                return;
            }
            prev = tmp;
            tmp = tmp->nextDiskPtr;
            if (tmp == NULL)
                break;
        }
        prev->nextDiskPtr = newDisk;
        newDisk->nextDiskPtr = tmp;
    }
    // adding when track is to the right of current
    else
    {
        procPtr prev = NULL;
        procPtr tmp = *diskReqQueue;
        
        while (tmp->track > newDisk->track)
        {
            prev = tmp;
            tmp = tmp->nextDiskPtr;
            if (tmp == NULL)
                break;
            if (tmp->track < head->track)
                break;
        }
        if (debugflag4)
            USLOSS_Console("\t\t wrapped around, tmp %d, prev %d\n", tmp == NULL ? -1 : tmp->track, prev->track);
        if (tmp == NULL)
        {
            prev->nextDiskPtr = newDisk;
            newDisk->nextDiskPtr = NULL;
            return;
        }
        while (tmp->track <= newDisk->track)
        {
            prev = tmp;
            tmp = tmp->nextDiskPtr;
            if (tmp == NULL)
                break;
        }
        if (debugflag4)
            USLOSS_Console("\t\t out of loop, tmp %d, prev %d\n", tmp == NULL ? -1 : tmp->track, prev->track);
        
        prev->nextDiskPtr = newDisk;
        newDisk->nextDiskPtr = tmp;
        return;
    }
    
    return;
} /* end of addDiskRequest */

/* ------------------------- printDiskReqQueue ----------------------------------- */
void printDiskReqQueue(procPtr* diskReqQueue)
{
    procPtr tmp = *diskReqQueue;
    while(tmp != NULL)
    {
        USLOSS_Console("\t printDiskReqQueue(): %d wants to %s on track %d\n", tmp->pid, tmp->opr == USLOSS_DISK_WRITE ? "write" : "read", tmp->track);
        tmp = tmp->nextDiskPtr;
    }
} /* end of printDiskReqQueue */

/* ------------------------- dequeueDiskReq ----------------------------------- */
void dequeueDiskReq(procPtr* diskQueue)
{
    procPtr head = *diskQueue;
    *diskQueue = head->nextDiskPtr;
    return;
}
