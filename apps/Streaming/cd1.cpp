/* 
 * Copyright (c) 2011-2014, University of Delaware
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */



#include <iostream>
#include <stdlib.h>
#include "darts.h"
#include "StreamingCodelet.h"

//#define INNER 1000
//#define OUTER 1000
#define INNER 15
#define OUTER 15
#define ARRAY_LENGTH 10000

using namespace darts;

class startCD : public Codelet 
{
public:
    Codelet * toSignal;
    startCD(uint32_t dep, uint32_t res, ThreadedProcedure * myTP, uint32_t stat, Codelet * toSig):
    Codelet(dep, res, myTP, stat),
    toSignal(toSig) { }

    virtual void fire(void)
    {
	//std::cout << "firing startCD" << std::endl;
        toSignal->decDep();
    }
};

class endCD : public Codelet
{
public:
    Codelet * toSignal;
    endCD(uint32_t dep, uint32_t res, ThreadedProcedure * myTP, uint32_t stat, Codelet * toSig):
    Codelet(dep, res, myTP, stat),
    toSignal(toSig) { }

    virtual void fire(void)
    {
        toSignal->decDep();
    }
};

class loadSCD : public StreamingCodelet<int, int>
{
public:
    loadSCD(uint32_t dep, uint32_t res, Codelet *consumerCod, ThreadedProcedure * myTP, uint32_t stat):
    StreamingCodelet(dep, res, consumerCod, myTP, stat) { }

    virtual void fire(void);
}; //loadSCD (streaming)

class copySCD : public StreamingCodelet<int, int>
{
public:
    Codelet * toSignal;
    copySCD(uint32_t dep, uint32_t res, Codelet *consumerCod, ThreadedProcedure * myTP, uint32_t stat, Codelet * toSig):
    StreamingCodelet(dep, res, consumerCod, myTP, stat),
    toSignal(toSig) { }

    virtual void fire(void);
}; //copySCD (streaming)


class aTP : public ThreadedProcedure
{
public:
    startCD startcd;
    //loadCD loadcd;
    loadSCD loadscd;
    //copyCD copycd;
    copySCD copyscd;
    endCD endcd;
    int x[ARRAY_LENGTH];
    int y[ARRAY_LENGTH];
    aTP(Codelet * toSig):
    ThreadedProcedure(),
    //startcd(0,0,this,0,&loadcd),
    startcd(0,0,this,0,&loadscd),
    //loadcd(1, 1, this,0,&copycd),
    loadscd(1, 1, &copyscd, this, 0),
    //copycd(1, 1, this,0,toSig)
    copyscd(1, 1, nullptr, this,0,&endcd), //streaming, but only consuming (so no consumercod ptr)
    endcd(1, 1, this, 0, toSig)
    {  
        add(&startcd);
	//add(&loadcd);
	add(&loadscd);
	//add(&copycd);
	add(&copyscd);
	//startcd.decDep();
	add(&endcd);
    }
};

void loadSCD::fire(void)
{
    std::cout << "loadSCD firing" << std::endl;
    //std::cout << "getConsumer produces: " << this->getConsumer() << std::endl;
    //MsgQFifo<int> * localFifo = dynamic_cast<MsgQFifo<int> *>(this->getConsumer());
    SoftFifo<int> * localFifo = dynamic_cast<SoftFifo<int> *>(this->getConsumer());
    //std::cout << "localFifo is " << localFifo << std::endl;
    int * arr = (dynamic_cast<aTP *>(myTP_))->x;
    for (int i=0; i<ARRAY_LENGTH; i++) {
        std::cout << "loadSCD iter" << i << std::endl;
        arr[i] = i;
        while(localFifo->push(arr[i]) != 0);
        //localFifo->push(arr[i]);
    }
    std::cout << "loadSCD done firing" << std::endl;
}

void copySCD::fire(void) 
{
    std::cout << "copySCD firing" << std::endl;
    //MsgQFifo<int> * localFifo = dynamic_cast<MsgQFifo<int> *>(this->getProducer());
    SoftFifo<int> * localFifo = dynamic_cast<SoftFifo<int> *>(this->getProducer());
    int * arr2 = (dynamic_cast<aTP *>(myTP_))->y;
    for (int i=0; i<ARRAY_LENGTH; i++) {
        std::cout << "copySCD iter" << i << std::endl;
        while(localFifo->pop(&(arr2[i])) != 0);
        //localFifo->pop(&(arr2[i]));
    }
    toSignal->decDep();
    std::cout << "copySCD done firing" << std::endl;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cout << "enter number of TPs and CDs" << std::endl;
        return 0;
    }

    int tps = atoi(argv[1]);
    int cds = atoi(argv[2]);
    uint64_t innerTime = 0;
    uint64_t outerTime = 0;
    
    ThreadAffinity affin(cds, tps, SPREAD, TPROUNDROBIN, MCSTANDARD);
    if (affin.generateMask())
    {
        Runtime * rt = new Runtime(&affin);

        for (int i = 0; i < OUTER; i++) 
        {
            rt->run(launch<aTP>(&Runtime::finalSignal));
            for (int j = 0; j < INNER; j++) 
            {
                uint64_t startTime = getTime();
                rt->run(launch<aTP>(&Runtime::finalSignal));
                uint64_t endTime = getTime();
                innerTime += endTime - startTime;
            }
            outerTime += innerTime / INNER;
	    //std::cout << "inner done" << std::endl;
            innerTime = 0;
        }
        std::cout << outerTime/OUTER << std::endl;
        delete rt;
    }
    return 0;
}
