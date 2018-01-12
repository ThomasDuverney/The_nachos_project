#include "process.h"
#include "system.h"

Process::Process(const char *pName) {
    pid = ++processCounter;
    if(currentThread == NULL) {
        ppid = pid; /* Le processus courant est le processus init */
    } else {
        ppid = currentThread->getPid();
    }
    processName = pName;
    launcherThread = new Thread(pName);
    launcherThread->setPid(pid);
    threadList = new std::map<int, Thread*>();
    threadList->insert(std::pair<int, Thread*>(launcherThread->getThreadID(), launcherThread));
}

Process::~Process(){
    delete threadList;
}

static void startUserProcess(int threadParams){
    currentThread->space->InitRegisters();
    currentThread->space->RestoreState();
    // Lance l'interpreteur mips -> banchement vers f.
    machine->Run();
}

void Process::startProcess(char * fileName){
    OpenFile *executable = fileSystem->Open(fileName);
    AddrSpace *space;

    if (executable == NULL){
        printf ("Unable to open file %s\n", fileName);
        ASSERT(FALSE);
    }
    space = new AddrSpace (executable);
    launcherThread->space = space;
    delete executable;      // close file

    if(threadCounter == 1){
        launcherThread->space->InitRegisters();
        launcherThread->space->RestoreState ();
        machine->Run();
    }else{
        launcherThread->Fork(startUserProcess, -1);
    }
}

void Process::finish(){
    for(std::map<int,Thread*>::iterator it=threadList->begin() ; it!=threadList->end() ; ++it){
        /* on détruit tous les thread de la liste sauf le thread courant qui doit être détruit par la methode finish (on travaille encore dans la pile d'éxécution de ce processus) -> threadTobedestroyed */
        if (it->second == currentThread){
            it->second->Finish(); /* it->second pour accèder à la valeur */
        } else {
            it->second->setStatus(TERMINATED);
        }
    }
    /* Suppression des threads TERMINATED de la ready list du scheduler */
    // Pas utilisé
    //IntStatus oldLevel = interrupt->SetLevel (IntOff);
    //scheduler->RemoveThreadFromReadyList();
    //(void) interrupt->SetLevel (oldLevel);
    unsigned i;
    TranslationEntry * pageTable = currentThread->space->getPageTable();
    for (i = 0; i < currentThread->space->getNumPages() ; i++) {
        frameProvider->ReleaseFrame( pageTable[i].physicalPage );
    }


}

void Process::addThread(Thread * newThread){
    threadList->insert(std::pair<int, Thread*>(newThread->getThreadID(), newThread));
    newThread->setPid(pid);
}

void Process::RemoveThread(int tid){
    threadList->erase(tid);
}

int Process::getPid() {
    return pid;
}

int Process::getPpid() {
    return ppid;
}

Thread* Process::getLauncherThread() {
    return launcherThread;
}

std::map<int,Thread*> *Process::getThreadList() {
  return threadList;
}

const char * Process::getProcessName(){
    return processName;
}
