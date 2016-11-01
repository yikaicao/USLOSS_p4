TARGET = libphase4.a
ASSIGNMENT = 452phase4
CC = gcc
AR = ar
COBJS = phase4.o libuser.o p1.o
CSRCS = ${COBJS:.o=.c}

PHASE1LIB = patrickphase1
PHASE2LIB = patrickphase2
PHASE3LIB = patrickphase3

#PHASE1LIB = patrickphase1debug
#PHASE2LIB = patrickphase2debug
#PHASE3LIB = patrickphase3debug

HDRS = phase1.h phase2.h phase3.h phase4.h providedPrototypes.h libuser.h

INCLUDE = ./usloss/include

CFLAGS = -Wall -g -std=gnu99 -I${INCLUDE} -I.  

UNAME := $(shell uname -s)

ifeq ($(UNAME), Darwin)
        CFLAGS += -D_XOPEN_SOURCE
endif

LDFLAGS += -L. -L./usloss/lib

PHASE4 = /home/cs452/fall15/phase4

ifeq ($(PHASE4), $(wildcard $(PHASE4)))
	LDFLAGS += -L$(PHASE4)
endif

TESTDIR = testcases

TESTS = test00 test01 test02 test03 test04 test05 test06 test07 test08 \
        test09 test10 test11 test12 test13 test14 test15 test16 test17 \
        test18 test19 test20 test21 test22 test23

LIBS = -lusloss -l$(PHASE1LIB) -l$(PHASE2LIB) -l$(PHASE3LIB) -lphase4

$(TARGET):	$(COBJS)
		$(AR) -r $@ $(COBJS) 

$(TESTS):	$(TARGET)
	$(CC) $(CFLAGS) -c $(TESTDIR)/$@.c
	$(CC) $(LDFLAGS) -o $@ $@.o $(LIBS)

clean:
	rm -f $(COBJS) $(TARGET) test*.txt test??.o test?? core term*.out

phase4.o:	

submit: $(CSRCS) $(HDRS) Makefile
	tar cvzf phase4.tgz $(CSRCS) $(HDRS) Makefile

