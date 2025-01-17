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


#include <unistd.h>
#include "TPSchedPolicy.h"
#include "TPScheduler.h"
#include "Codelet.h"
#include "StreamingCodelet.h"
#include "MicroScheduler.h"
#include <cstdlib>
#include "tpClosure.h"
#ifdef TRACE
#include "getClock.h"
#endif

namespace darts {

    // should only be called after checking if Codelet being popped/pushed
    // (producerCod) is a StreamingCodelet
    Fifo *
    TPRoundRobin::allocateFifo(Codelet * producerCod) {
        //std::cout << "TPScheduler allocating Fifo" << std::endl;
        // TODO
	    // Make scheduling decision here -- not now but in the future
	    // for example, decDep consumer and see if it is ready; if its not yet
	    // then store farther away. If it is, use HW Fifo when available 
	    // new Fifo, set producer/consumer values on Fifo
	    Fifo * streamFifo = producerCod->generateFifo(0, 0, 0, 10, producerCod->getConsumerCod());
	    producerCod->setConsumer(streamFifo);
	    (producerCod->getConsumerCod())->setProducer(streamFifo);
	    // here we're not setting producer because this Codelet does not have a Fifo
	    // producing for it -- or if it does then it is already assigned 
	    // add Fifo to SU-managed table
	    fifos_.push(streamFifo);
	    //consumer should have only 1 dep; all others are intrinsic through producer Codelet
	    producerCod->decDepConsumerCod();
	    //std::cout << "consumer fifo points to " << producerCod->getConsumer() << std::endl;
	    //std::cout << "producer fifo points to " << (producerCod->getConsumerCod())->getProducer() << std::endl; 
	    //std::cout << "Fifo allocated" << std::endl;
	    // set Fifo address on StreamingCodelet(s)
	    return(streamFifo);
    }

    // Creates TPs from TPClosures; pops Codelets from queue and distributes them evenly to MCSchedulers
    void
    TPRoundRobin::policy() {
        useconds_t usecs = 1,
                   range = 1;
#ifdef TRACE
        addRecord(getTime(), (void*) &TPRoundRobin::policy);
#endif       
        while (alive()) {
            //Check if we have any work in our deque
            tpClosure * tempClosure;
            if (!(tempClosure = popTP()))
                tempClosure = steal();

            if (tempClosure) {
                usecs = range; // reset sleep time
#ifdef TRACE
                addRecord(getTime(), (void*) tempClosure->factory);
#endif
                tempClosure->factory(tempClosure);
#ifdef TRACE
                addRecord(getTime(), (void*) &TPRoundRobin::policy);
#endif
                delete tempClosure;
                //Get the work ready!
            } else {
                usleep(usecs);
                if (usecs < 500)
                    usecs *= 2;
            }
            //Lets do the work!
            Codelet * tempCodelet = popCodelet();
	    //check if Codelet expects streamed input/output
	    //if it is Streaming but doesn't have a consumer Codelet, it is the end of a pipeline
	    if (tempCodelet) { //check in case pop returns nullptr
	        if (tempCodelet->isStreaming() && (tempCodelet->getConsumerCod() != nullptr)) {
                    this->allocateFifo(tempCodelet);
                }
	    }
	    //allocate Fifo (later can possibly reuse allocated Fifos depending on settings?)
	    //update Fifo book keeping
	    //set Codelet's pointer(s)
	    //need a second variable to indicate which codelet is connected to this one.
	    //could go with unique Fifo IDs
            while (tempCodelet) {
                MScheduler * myCDS = static_cast<MScheduler*> (getSubScheduler(getSubIndexInc()));
                //std::cout << "TPScheduler trying to push codelet . . ." << std::endl;
                while (!myCDS->pushCodelet(tempCodelet)) {
                    myCDS = static_cast<MScheduler*> (getSubScheduler(getSubIndexInc()));
                }
                //std::cout << "Codelet successfully pushed" << std::endl;
                tempCodelet = popCodelet();
            }
        }
    }

    // Creates TPs; pops codelets and attempts to push them to MCSchedulers; if it fails, executes Codelet itself
    void
    TPPushFull::policy() {
        useconds_t usecs = 1, 
                   range = 1;
#ifdef TRACE
        addRecord(getTime(), (void*) &TPPushFull::policy);
#endif
        while (alive()) {
            //Check if we have any work in our deque
            tpClosure * tempClosure = popTP();
            if (!tempClosure)
                tempClosure = steal();

            if (tempClosure) {
                usecs = range; // reset sleep time
#ifdef TRACE
                addRecord(getTime(), (void*) tempClosure->factory);
#endif
                tempClosure->factory(tempClosure);
#ifdef TRACE
                addRecord(getTime(), (void*) &TPPushFull::policy);
#endif
                delete tempClosure;
                //Get the work ready!
            } else {
                usleep(usecs);
                if (usecs < 500)
                    usecs *= 2;
           }
            //Lets do the work!
            Codelet * tempCodelet = popCodelet();
            while (tempCodelet) {
                //Here we are going to try to push
                bool fail = true;
                for (size_t i = 0; i < getNumSub(); i++) {
                    MScheduler * myCDS = static_cast<MScheduler*> (getSubScheduler(getSubIndexInc()));
                    if (myCDS->pushCodelet(tempCodelet)) {
                        fail = false;
                        break;
                    }
                }
                //If we fail do it ourself
                if (fail) {
                    ThreadedProcedure * checkTP = tempCodelet->getTP();
                    bool deleteTP = (checkTP) ? checkTP->checkParent() : false;
#ifdef TRACE
                    addRecord(getTime(), tempCodelet->returnFunct());
#endif
#ifdef COUNT
		    if(getAffinity()) getAffinity()->startCounters(getID());
#endif
		    tempCodelet->fire();
#ifdef COUNT
		    if(getAffinity()) getAffinity()->incrementCounters(getID());
#endif
#ifdef TRACE
                    addRecord(getTime(), (void*) &TPPushFull::policy);
#endif
                    if (deleteTP) {
                        if (checkTP->decRef())
                            delete checkTP;
                    }
                }
                tempCodelet = popCodelet();
            }
        }
    }

    // Creates TPs; pops Codelets and fires them; DOES NOT distribute codelets to MCSchedulers
    void
    TPStatic::policy() {
        useconds_t usecs = 1, 
                   range = 1;
#ifdef TRACE
        addRecord(getTime(), (void*) &TPStatic::policy);
#endif
        while (alive()) {
            //Check if we have any work in our deque
            tpClosure * tempClosure = popTP();
            if (!tempClosure)
                tempClosure = steal();

            if (tempClosure) {
                usecs = range; // reset sleep time
#ifdef TRACE
                addRecord(getTime(), (void*) tempClosure->factory);
#endif
                tempClosure->factory(tempClosure);
#ifdef TRACE
                addRecord(getTime(), (void*) &TPStatic::policy);
#endif
                delete tempClosure;
                //Get the work ready!
            } else {
                usleep(usecs);
                if (usecs < 500)
                    usecs *= 2;
           }

            Codelet * tempCodelet = popCodelet();
            while (tempCodelet) {
                ThreadedProcedure * checkTP = tempCodelet->getTP();
                bool deleteTP = (checkTP) ? checkTP->checkParent() : false;
#ifdef TRACE
                addRecord(getTime(), tempCodelet->returnFunct());
#endif
#ifdef COUNT
		    if(getAffinity()) getAffinity()->startCounters(getID());
#endif
		    tempCodelet->fire();
#ifdef COUNT
		    if(getAffinity()) getAffinity()->incrementCounters(getID());
#endif
#ifdef TRACE
                addRecord(getTime(), (void*) &TPStatic::policy);
#endif
                if (deleteTP) {
                    if (checkTP->decRef())
                        delete checkTP;
                }
                tempCodelet = popCodelet();
            }
        }
    }

    // This is probably defined so MCSchedulers can push Codelets to TPScheduler
    // since TPStatic, aside from creating TPs, only pops codelets and fires them
    bool
    TPStatic::pushCodelet(Codelet * CodeletToPush) 
    {
        uint64_t status = CodeletToPush->getStatus();
        size_t numSub = getNumSub();
        if (!status || !numSub)
        {
            return codelets_.push(CodeletToPush);
        }
        MScheduler * myCDS = static_cast<MScheduler*> (getSubScheduler((status - 1) % numSub));
        return myCDS->pushCodelet(CodeletToPush);
    }

    /* TODO: make this work with other MCSchedPolicies. DO NOT use this or StreamingCodelets with the
             MCDYNAMIC policy; MCDYNAMIC policy pops codelets from TPScheduler and creates a race condition
             where StreamingCodelets may try to access Fifo without one being allocated
    */
    Fifo *
    TPDynamic::allocateFifo(Codelet * producerCod) {
        //std::cout << "TPScheduler allocating Fifo" << std::endl;
        // TODO
	    // Make scheduling decision here -- not now but in the future
	    // for example, decDep consumer and see if it is ready; if its not yet
	    // then store farther away. If it is, use HW Fifo when available 
	    // new Fifo, set producer/consumer values on Fifo
	    Fifo * streamFifo = producerCod->generateFifo(0, 0, 0, 10, producerCod->getConsumerCod());
	    producerCod->setConsumer(streamFifo);
	    (producerCod->getConsumerCod())->setProducer(streamFifo);
	    // here we're not setting producer because this Codelet does not have a Fifo
	    // producing for it -- or if it does then it is already assigned 
	    // add Fifo to SU-managed table
	    fifos_.push(streamFifo);
	    //consumer should have only 1 dep; all others are intrinsic through producer Codelet
	    producerCod->decDepConsumerCod();
	    //std::cout << "consumer fifo points to " << producerCod->getConsumer() << std::endl;
	    //std::cout << "producer fifo points to " << (producerCod->getConsumerCod())->getProducer() << std::endl; 
	    //std::cout << "Fifo allocated" << std::endl;
	    // set Fifo address on StreamingCodelet(s)
	    return(streamFifo);
    }

    // Creates TPs, pops codelets from own queue and fires them; DOES NOT distribute codelets
    void
    TPDynamic::policy() {
        useconds_t usecs = 1, 
                   range = 1;
#ifdef TRACE
        addRecord(getTime(), (void*) &TPDynamic::policy);
#endif
        while (alive()) {
            //Check if we have any work in our deque
            tpClosure * tempClosure;
            if (!(tempClosure = popTP()))
                tempClosure = steal();

            if (tempClosure) {
                usecs = range; // reset sleep time;
#ifdef TRACE
                addRecord(getTime(), (void*) tempClosure->factory);
#endif
                tempClosure->factory(tempClosure);
#ifdef TRACE
                addRecord(getTime(), (void*) &TPDynamic::policy);
#endif
                delete tempClosure;
                //Get the work ready!
            } else {
                usleep(usecs);
                if (usecs < 500)
                    usecs *= 2;
           }

            //Lets do the work!
            Codelet * tempCodelet = popCodelet();
	    if (tempCodelet) { //make sure not nullptr before accessing methods
                if (tempCodelet->isStreaming() && (tempCodelet->getConsumerCod() != nullptr)) {
                    //std::cout << "inside TPScheduler streaming-if statement" << std::endl;
                    this->allocateFifo(tempCodelet);
                }
            }
            while (tempCodelet) {
                ThreadedProcedure * checkTP = tempCodelet->getTP();
                bool deleteTP = (checkTP) ? checkTP->checkParent() : false;
#ifdef TRACE
                addRecord(getTime(), tempCodelet->returnFunct());
#endif
#ifdef COUNT
		    if(getAffinity()) getAffinity()->startCounters(getID());
#endif
		    tempCodelet->fire();
#ifdef COUNT
		    if(getAffinity()) getAffinity()->incrementCounters(getID());
#endif
#ifdef TRACE
                addRecord(getTime(), (void*) &TPDynamic::policy);
#endif
                if (deleteTP) {
                    if (checkTP->decRef())
                        delete checkTP;
                }

                tempCodelet = popCodelet();
		// TODO: add mechanism for bookkeeping (deleting Fifos that are out of use)
		if (tempCodelet) { //make sure not nullptr before accessing methods
	            if (tempCodelet->isStreaming() && (tempCodelet->getConsumerCod() != nullptr)) {
		        //std::cout << "inside TPScheduler streaming-if statement" << std::endl;
                        this->allocateFifo(tempCodelet);
                    }
		}
            } //while tempCodelet
        } //while alive
	//std::cout << "clearing Fifos" << std::endl;
	this->clearFifos();
    }

    // Makes TPs; Distributes Codelets; DOES NOT fire codelets ever
    void
    TPWorkPush::policy() {
        useconds_t usecs = 1,
                   range = 1;
#ifdef TRACE
        addRecord(getTime(), (void*) &TPWorkPush::policy);
#endif
        while (alive()) {
            //Check if we have any work in our deque
            tpClosure * tempClosure = popTP();

            if (tempClosure) {
                usecs = range; // reset sleep time
#ifdef TRACE
                addRecord(getTime(), (void*) tempClosure->factory);
#endif
                tempClosure->factory(tempClosure);
#ifdef TRACE
                addRecord(getTime(), (void*) &TPWorkPush::policy);
#endif
                delete tempClosure;
                //Get the work ready!
            } else {
                usleep(usecs);
                if (usecs < 500)
                    usecs *= 2;
           }
            //Lets do the work!
            Codelet * tempCodelet = popCodelet();
            while (tempCodelet) {
                //Here we are going to try to push
                bool fail = true;
                while (fail) {
                    MScheduler * myCDS = static_cast<MScheduler*> (getSubScheduler(getSubIndexInc()));
                    if (myCDS->pushCodelet(tempCodelet)) {
                        fail = false;
                        break;
                    }
                }
                tempCodelet = popCodelet();
            }
        }
    }

    TPScheduler *
    TPScheduler::create(unsigned int type) 
    {
        if (type == TPROUNDROBIN) return new TPRoundRobin; // Creates TPs from TPClosures; pops Codelets from queue and distributes them evenly to MCSchedulers
        if (type == TPPUSHFULL) return new TPPushFull;     // Creates TPs; pops codelets and attempts to push them to MCSchedulers; if it fails, executes Codelet itself
        if (type == TPSTATIC) return new TPStatic;         // Creates TPs; pops Codelets and fires them; DOES NOT distribute codelets to MCSchedulers
        if (type == TPDYNAMIC) return new TPDynamic;       // Creates TPs, pops codelets from own queue and fires them; DOES NOT distribute codelets
        else return NULL;
    }



} // Namespace darts
