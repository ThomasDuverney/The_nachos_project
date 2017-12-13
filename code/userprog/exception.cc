// exception.cc
//      Entry point into the Nachos kernel from user programs.
//      There are two kinds of things that can cause control to
//      transfer back to here from user code:
//
//      syscall -- The user code explicitly requests to call a procedure
//      in the Nachos kernel.  Right now, the only function we support is
//      "Halt".
//
//      exceptions -- The user code does something that the CPU can't handle.
//      For instance, accessing memory that doesn't exist, arithmetic errors,
//      etc.
//
//      Interrupts (which can also cause control to transfer from user
//      code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"

void copyStringFromMachine(int from, char *to, unsigned size){
    unsigned int i=0;
    int c;
    while(i<(size-1) && machine->ReadMem(from+i, 1, &c) && (char)c != '\0') {
        *(to+i) = (char)c;
        i++;
    }
    *(to+i) = '\0';
}

//----------------------------------------------------------------------
// UpdatePC : Increments the Program Counter register in order to resume
// the user program immediately after the "syscall" instruction.
//----------------------------------------------------------------------
static void UpdatePC (){
    int pc = machine->ReadRegister (PCReg);
    machine->WriteRegister (PrevPCReg, pc);
    pc = machine->ReadRegister (NextPCReg);
    machine->WriteRegister (PCReg, pc);
    pc += 4;
    machine->WriteRegister (NextPCReg, pc);
}


//----------------------------------------------------------------------
// ExceptionHandler
//      Entry point into the Nachos kernel.  Called when a user program
//      is executing, and either does a syscall, or generates an addressing
//      or arithmetic exception.
//
//      For system calls, the following is the calling convention:
//
//      system call code -- r2
//              arg1 -- r4
//              arg2 -- r5
//              arg3 -- r6
//              arg4 -- r7
//
//      The result of the system call, if any, must be put back into r2.
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//      "which" is the kind of exception.  The list of possible exceptions
//      are in machine.h.
//----------------------------------------------------------------------

void ExceptionHandler (ExceptionType which){
    int type, reg4;
    type = machine->ReadRegister (2);
    reg4 = machine->ReadRegister (4);
    char c;


    if (which == SyscallException) {
        switch (type) {
            case SC_Halt:
                DEBUG('a', "Shutdown, initiated by user program.\n");
                interrupt->Halt();
                break;
            case SC_PutChar:
                DEBUG('a', "PutChar, initiated by user program.\n");
                synchconsole->SynchPutChar((char)reg4);
                break;
            case SC_SynchPutString:
                DEBUG('a', "SynchPutString, initiated by user program.\n");
                char buf [MAX_STRING_SIZE];
                copyStringFromMachine(reg4, buf, MAX_STRING_SIZE);
                synchconsole->SynchPutString(buf);
                break;
            case SC_SynchGetChar:
                DEBUG('a', "GetChar, initiated by user program.\n");
                c = synchconsole->SynchGetChar();
                if(c != EOF){
                  machine->WriteRegister(2, (int)c);
                }
                break;
            case SC_SynchGetString:
                DEBUG('a', "SynchGetString, initiated by user program.\n");
                synchconsole->SynchGetString(&machine->mainMemory[reg4], reg5);
                machine->WriteRegister(2, reg4);
                break;
            case SC_Exit:
                // la valeur de retour du main ou exit est dans le registre 4
                DEBUG('a', "Exit, initiated by user program.\n");
                break;
            default:
                printf("Unexpected user mode exception %d %d\n", which, type);
                ASSERT(FALSE);
        }
    }

    // LB: Do not forget to increment the pc before returning!
    UpdatePC();
    // End of addition
}
