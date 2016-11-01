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
int semRunning;
procStruct ProcTable[MAXPROC];
procPtr sleepList;

// driver processes
static int ClockDriver(char *);
static int DiskDriver(char *);

// system helpers
void sleep(systemArgs *);
int sleepReal(int);

// kernel helpers
void check_kernel_mode(char *);
void setUserMode();
void initSysCallVec();
void initProcTable();
void clearProcess(int);
void printProcTable();
void addSleepRequest(procPtr*, procPtr);
void printSleepList();

void start3(void)
{
    char    name[128]; // name for driver processes
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
     * Initialization
     */
    initSysCallVec();
    initProcTable();
    sleepList = NULL;
    
    /*
     * Create clock device driver
     * I am assuming a semaphore here for coordination.  A mailbox can
     * be used instead -- your choice.
     */
    semRunning = semcreateReal(0);
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
    
    /*
     * Create the disk device drivers here.  You may need to increase
     * the stack size depending on the complexity of your
     * driver, and perhaps do something with the pid returned.
     */
    
//    for (i = 0; i < USLOSS_DISK_UNITS; i++) {
//        sprintf(buf, "%d", i);
//        pid = fork1(name, DiskDriver, buf, USLOSS_MIN_STACK, 2);
//        if (pid < 0) {
//            USLOSS_Console("start3(): Can't create term driver %d\n", i);
//            USLOSS_Halt(1);
//        }
//    }
//    sempReal(semRunning);
//    sempReal(semRunning);
    
    /*
     * Create terminal device drivers.
     */
    
    
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
    
    return 0;
} /* end of ClockDriver */

/* ------------------------- DiskDriver ----------------------------------- */
static int DiskDriver(char *arg)
{
    // Let the parent know we are running and enable interrupts.
    semvReal(semRunning);
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
    
    int unit = atoi( (char *) arg); 	// Unit is passed as arg.
    return unit;
} /* end of DiskDriver */




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
    
    procPtr newSleep = &ProcTable[getpid() % MAXPROC];
    newSleep->nextSleepPtr = NULL;
    newSleep->wakeTime = wakeTime;
    newSleep->pid = getpid();
    
    addSleepRequest(&sleepList, newSleep);
    
    //debug
    //printSleepList();
    //printProcTable();
    
    MboxReceive(newSleep->privateMboxID, 0, 0);
    
    
    return 0;
} /* end of sleepReal */



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
    
    if (*list == NULL)
    {
        *list = newSleep;
    }
    
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
