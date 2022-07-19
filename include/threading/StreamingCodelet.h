#pragma once
#include <stdint.h>
#include "Codelet.h"
namespace darts
{

    template <typename inputData, typename outputData>
    class StreamingCodelet : public Codelet {
    protected:
	    Fifo *producer_; //assigned by TP scheduler when dependencies fulfilled
	    Fifo *consumer_; //same here. These can both be active
	Codelet * consumerCod_; //pipelining chain goes downwards so only contains consumer Codelet pointer
	//think of it like a linked list of streaming codelets managed by the SU
	virtual bool isStreaming() { return true; }
    public:
        StreamingCodelet() :
	    Codelet()
	    { };
        StreamingCodelet(Codelet *consumerCod) :
	    Codelet(),
	    consumerCod_(consumerCod)
	    { };
        StreamingCodelet(uint32_t dep, uint32_t res, Codelet *consumerCod, ThreadedProcedure * theTp=NULL, uint32_t stat=SHORTWAIT) :
	    Codelet(dep, res, theTp, stat),
	    consumerCod_(consumerCod)
	    { };
	Fifo * getConsumer() { return(consumer_); } //maybe get rid of these
	Fifo * getProducer() { return(producer_); } // and replace with just pop/push
	void setProducer(Fifo *producer) { this->producer_ = producer; }
	void setConsumer(Fifo *consumer) { this->consumer_ = consumer; }
	Codelet * getConsumerCod() { return(consumerCod_); }
	void setConsumerCod(Codelet *consumerCod) { this->consumerCod_ = consumerCod; }
	virtual void decDepConsumerCod() { (this->consumerCod_)->decDep(); }
        virtual Fifo * generateFifo(const uint64_t cluster, const uint64_t localMem, const uint64_t id, const uint64_t size, Codelet *consumer) {
		Fifo * streamFifo = new SoftFifo<outputData>(0, 0, 0, 10, this, this->getConsumerCod()); 
	    return(streamFifo);
	}
	/* The generateFifo method is only attached here as a way to resolve
	 * the typing issue with template subclass(es) of Fifo and so the SU
	 * (i.e. TPScheduler and TPSchedPolicy don't have to be aware of the
	 * Fifo type). This should probably change structure later.
	 * This method SHOULD NOT be called by a user; it exists only to be
	 * called by the scheduling mechanism.
	 */
	//copied from .cpp

    };
} //namespace darts
