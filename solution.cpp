#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include "common.h"
using namespace std;
#endif /* __PROGTEST__ */

uint32_t* memory = NULL;

uint8_t procCnt = 0;
pthread_mutex_t procCntMtx;
pthread_cond_t procCv;

bool* isPageFree = NULL;
uint32_t totalPagesCnt = 0;
uint32_t freePagesCnt = 0;
pthread_mutex_t pagesMtx;


void* runProc(void*);


void fillPageTableWithZeros(uint32_t page) 
{
    uint32_t limit = page * 1024 + 1024;

    for (uint32_t i = page * 1024; i < limit; ++i)
        memory[i] = 0;
}


uint32_t nextFreePage() 
{
    uint32_t i;

    for (i = 0; i < totalPagesCnt; ++i)
        if (isPageFree[i]) 
            break;
    
    if (i == totalPagesCnt)
        throw "no_more_pages";

    freePagesCnt--;
    isPageFree[i] = false;

    return i;
}


bool checkSpace(uint32_t increment, uint32_t pageTableRoot)
{
    uint32_t needed = increment;
    pageTableRoot = pageTableRoot >> 2;

    uint32_t firstLvl;
    uint32_t limit = pageTableRoot + 1024;

    //find one after last entry in 1st lvl page table
    for (firstLvl = pageTableRoot; firstLvl < limit; ++firstLvl)
        if (!(memory[firstLvl] & CCPU::BIT_PRESENT)) 
            break;
    

    if (firstLvl == pageTableRoot)
        needed++;
    else
        firstLvl--;

    //second level
    uint32_t secondLvlTable = ((memory[firstLvl] & CCPU::ADDR_MASK) >> 2);
    limit = secondLvlTable + 1024;

    for (uint32_t i = 0; i < increment; ++i) {
        uint32_t currentEntry;

        //check if another 2nd lvl page table will be neccessary for reallocation
        for (currentEntry = secondLvlTable; currentEntry < limit; ++currentEntry) 
            if (!(memory[currentEntry] & CCPU::BIT_PRESENT)) 
                break;

        if (currentEntry == limit) { 
            if (currentEntry == pageTableRoot + 1022) 
                return false;
            
            currentEntry = 0;
            limit = 1024;
            needed++;
        }

        currentEntry++;
    }


    if (needed > freePagesCnt) return false;
    return true;
}


uint32_t addPage(bool createFirstLvl, uint32_t pageTableRoot = 0) 
{
    uint32_t newPage;
    pageTableRoot = pageTableRoot >> 2;

    //create 1st lvl table
    if (createFirstLvl) {
        newPage = nextFreePage();
        fillPageTableWithZeros(newPage);
    } 

    //allocate new page and add it to the end of last 2nd lvl page table
    else {
        uint32_t firstLvl;
        uint32_t limit = pageTableRoot + 1024;

        //find one after last entry in 1st lvl page table
        for (firstLvl = pageTableRoot; firstLvl < limit; firstLvl++)
            if (!(memory[firstLvl] & CCPU::BIT_PRESENT)) 
                break;

        //if 1st lvl page table is empty, allocate 2nd lvl page table and add it to 1st lvl page table
        if (firstLvl == pageTableRoot) {
            newPage = nextFreePage();
            fillPageTableWithZeros(newPage);
            memory[firstLvl] = (newPage << 12) | CCPU::BIT_PRESENT | CCPU::BIT_WRITE | CCPU::BIT_USER;;
        } 
        //move to last entry
        else {
            firstLvl--;
        }

        uint32_t secondLvlTable = (memory[firstLvl] & CCPU::ADDR_MASK) >> 2;
        limit = secondLvlTable + 1024;
        uint32_t secondLvlLastEntry;

        //find one after last entry in 2nd lvl page table  
        for (secondLvlLastEntry = secondLvlTable; secondLvlLastEntry < limit; ++secondLvlLastEntry)
            if (!(memory[secondLvlLastEntry] & CCPU::BIT_PRESENT)) 
                break;
        
        //if 2nd lvl page table is full, allocate new + update 1st lvl page table
        if (secondLvlLastEntry == limit) {
            if (firstLvl == pageTableRoot + 1023) 
                throw "no_more_space_in_1st_lvl_page_table";

            newPage = nextFreePage();
            fillPageTableWithZeros(newPage);

            memory[firstLvl + 1] = (newPage << 12)| CCPU::BIT_PRESENT | CCPU::BIT_WRITE | CCPU::BIT_USER;;
            secondLvlLastEntry = newPage * 1024;
        }


        //allocate page and add entry to 2nd lvl page table
        newPage = nextFreePage();
        memory[secondLvlLastEntry] = (newPage << 12) | CCPU::BIT_PRESENT | CCPU::BIT_WRITE | CCPU::BIT_USER;
    }

    return newPage << 12;
}


void removePage(uint32_t pageTableRoot, bool deallocateFirstLvl = false)
{
    pageTableRoot = pageTableRoot >> 2;

    if (deallocateFirstLvl) {
        isPageFree[pageTableRoot >> 10] = true;
        memory[pageTableRoot] = 0;
        freePagesCnt++;
    } 

    else {
        uint32_t firstLvlLastEntry;
        uint32_t limit = pageTableRoot + 1024;

        //find one after last entry in 1st lvl page table, then go back 1 entry to the last one
        for (firstLvlLastEntry = pageTableRoot; firstLvlLastEntry < limit; ++firstLvlLastEntry)
            if (!(memory[firstLvlLastEntry] & CCPU::BIT_PRESENT)) 
                break;
        firstLvlLastEntry--;

        uint32_t secondLvlTable = ((memory[firstLvlLastEntry] & CCPU::ADDR_MASK) >> 2);
        uint32_t secondLvlLastEntry;
        uint32_t page;
        limit = secondLvlTable + 1024;

        //find one after last entry in 2nd lvl page table, then go back 1 entry to the last one
        for (secondLvlLastEntry = secondLvlTable; secondLvlLastEntry < limit; ++secondLvlLastEntry) 
            if (!(memory[secondLvlLastEntry] & CCPU::BIT_PRESENT)) 
                break;
        secondLvlLastEntry--;

        page = memory[secondLvlLastEntry] >> 12;
        isPageFree[page] = true;
        freePagesCnt++;
        memory[secondLvlLastEntry] = 0;

        //delete 2nd lvl page table if its empty
        if (secondLvlLastEntry == secondLvlTable) {
            isPageFree[memory[firstLvlLastEntry] >> 12] = true;
            memory[firstLvlLastEntry] = 0;
            freePagesCnt++;
        }
    }
}

uint32_t copyMemory(uint32_t pageTableRoot) 
{
    uint32_t newPageTableRoot = addPage(true);
    uint32_t firstLvlCnter = 0;
    uint32_t secondLvlCnter = 0;

    pageTableRoot = pageTableRoot >> 2;

    //copy pages from 1st lvl page table
    while (memory[pageTableRoot + firstLvlCnter] & CCPU::BIT_PRESENT){
        uint32_t secondLvlPageTable = (memory[pageTableRoot + firstLvlCnter] & CCPU::ADDR_MASK) >> 2;
        uint32_t newSecondLvlPageTable = nextFreePage();
        newSecondLvlPageTable = newSecondLvlPageTable << 10;

        memory[(newPageTableRoot >> 2) + firstLvlCnter] = (newSecondLvlPageTable << 2) | CCPU::BIT_PRESENT | CCPU::BIT_WRITE | CCPU::BIT_USER;

        //copy pages from 2nd lvl page table
        while (memory[secondLvlPageTable + secondLvlCnter] & CCPU::BIT_PRESENT) {
            uint32_t newPage = nextFreePage();

            memcpy( &memory[newPage << 10], 
                    &memory[ (memory[secondLvlPageTable + secondLvlCnter] & CCPU::ADDR_MASK) >> 2], 
                    1024 * sizeof(uint32_t)
                   );

            memory[newSecondLvlPageTable + secondLvlCnter] = (newPage << CCPU::OFFSET_BITS) | CCPU::BIT_PRESENT | CCPU::BIT_WRITE | CCPU::BIT_USER;

            secondLvlCnter++;
        }

        secondLvlCnter = 0;
        firstLvlCnter++;
    }


    return newPageTableRoot;
}



class runParams {
public:
    uint32_t pageTableRoot;
    void* processArg;
    void (* entryPoint) (CCPU*, void*);
    uint32_t procPagesCnt;

    runParams(uint32_t pageTableRoot_, void * processArg_, void (* entryPoint_) (CCPU*, void*), uint32_t procPagesCnt_ = 0)
    : pageTableRoot(pageTableRoot_), processArg(processArg_), entryPoint(entryPoint_), procPagesCnt(procPagesCnt_) 
    {
    }
};



class CPU2 : public CCPU {

public:

    CPU2(uint8_t* memStart, uint32_t pageTableRoot, uint32_t procPagesCnt_) //initialise memory and updates number of running processes
    : CCPU(memStart, pageTableRoot), procPagesCnt(procPagesCnt_) 
    {
        pthread_mutex_lock(&procCntMtx);
        procCnt++;
        pthread_mutex_unlock(&procCntMtx);
    };



    virtual ~CPU2() 
    {    
        pthread_mutex_lock(&pagesMtx);
        for (uint32_t i = 0; i < procPagesCnt; i++)
            removePage(m_PageTableRoot);
        removePage(m_PageTableRoot, true);
        pthread_mutex_unlock(&pagesMtx);

        pthread_mutex_lock(&procCntMtx);
        procCnt--;
        if (procCnt == 0) 
            pthread_cond_signal(&procCv);
        pthread_mutex_unlock(&procCntMtx);
    }




    virtual uint32_t GetMemLimit(void) const 
    {
        return procPagesCnt;
    }




    virtual bool SetMemLimit(uint32_t pages)
    {
        if (pages == procPagesCnt) return true; //no need to change size
   
        uint32_t diff;

        //increasing page cnt
        if (pages > procPagesCnt) { 
            diff = pages - procPagesCnt;
            pthread_mutex_lock(&pagesMtx);
            if (checkSpace(diff, m_PageTableRoot)) {
                
                for (uint32_t i = 0; i < diff; i++) 
                    try {
                        addPage(false, m_PageTableRoot);
                    } catch (...) {
                        pthread_mutex_unlock(&pagesMtx);
                        return false;
                    }
                
            } else {
                pthread_mutex_unlock(&pagesMtx);
                return false;
            }
            pthread_mutex_unlock(&pagesMtx);
        } 

        //decreasing page cnt
        else {
            diff = procPagesCnt - pages;
            pthread_mutex_lock(&pagesMtx);
            for (uint32_t i = 0; i < diff; ++i)
                removePage(m_PageTableRoot);
            pthread_mutex_unlock(&pagesMtx);
        }

        procPagesCnt = pages;
        return true;
    }



    virtual bool NewProcess(void * processArg, void (* entryPoint) (CCPU *, void *), bool copyMem) 
    {   
        pthread_mutex_lock(&procCntMtx);
        if(procCnt >= 64){
            pthread_mutex_unlock(&procCntMtx);
            return false;
        }
        pthread_mutex_unlock(&procCntMtx);

        try {
            pthread_attr_t thrAttr;
            pthread_t thr;
            pthread_attr_init(&thrAttr);
            pthread_attr_setdetachstate(&thrAttr, PTHREAD_CREATE_DETACHED);
            uint32_t mem = 0;
            uint32_t newPageTableRoot;

            if (copyMem) {
                pthread_mutex_lock(&pagesMtx);
                if (!checkSpace(procPagesCnt + procPagesCnt / 1024 + 1, m_PageTableRoot)) {
                    pthread_mutex_unlock(&pagesMtx);
                    return false;
                }

                newPageTableRoot = copyMemory(m_PageTableRoot);
                mem = procPagesCnt;
                pthread_mutex_unlock(&pagesMtx);

            } else {
                pthread_mutex_lock(&pagesMtx);
                newPageTableRoot = addPage(true);
                pthread_mutex_unlock(&pagesMtx);
            }

            runParams* params = new runParams(newPageTableRoot, processArg, entryPoint, mem);
            pthread_create(&thr, &thrAttr, runProc, params);

        } catch (...) {
            pthread_mutex_unlock(&pagesMtx);
            return false;
        }
        return true;
    }

protected:

    uint32_t procPagesCnt = 0;
};




void* runProc(void* params_) 
{
    runParams* params = (runParams*) params_;
    CPU2* cpu = new CPU2( (uint8_t*) memory, params->pageTableRoot, params->procPagesCnt);

    params->entryPoint(cpu, params->processArg);

    delete cpu;
    delete params;
    return NULL;
}




void MemMgr(void* mem, uint32_t totalPages, void* processArg, void (* mainProcess) (CCPU *, void *)) 
{
    memory = (uint32_t*) mem;
    isPageFree = new bool[totalPages];
    freePagesCnt = totalPagesCnt = totalPages;


    pthread_mutex_init(&procCntMtx, NULL);
    pthread_mutex_init(&pagesMtx, NULL);
    pthread_cond_init(&procCv, NULL);

    for(uint32_t i = 0; i < totalPagesCnt; ++i)
        isPageFree[i] = true;

    uint32_t initPageTableRoot = addPage(true);
    runParams* params = new runParams(initPageTableRoot, processArg, mainProcess);


    runProc(params);


    pthread_mutex_lock(&procCntMtx);
    while (procCnt != 0) 
        pthread_cond_wait(&procCv, &procCntMtx);
    pthread_mutex_unlock(&procCntMtx);


    pthread_mutex_destroy(&procCntMtx);
    pthread_mutex_destroy(&pagesMtx);
    pthread_cond_destroy(&procCv);
    delete [] isPageFree;
}

