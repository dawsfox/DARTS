
#ifndef DARTS_HWLOC_FIFO_H
#define DARTS_HWLOC_FIFO_H

#include <vector>
#include "Lock.h"
#include "Codelet.h"
#include "MsgQ.hpp"
#include "ringbuffer.h"

namespace darts {
	
//FifoMeta class doesn't deal with actual data elements so
//it doesn't need to be a template. This makes it easier
//for the SU to have Fifos of different types in the same
//structure for overall bookkeeping
    class FifoMeta {
    public:
        uint64_t _cluster,
		 _localMem,
		 _id,
		 _size,
		 _typeSize;
	Codelet * _producer,
	        * _consumer;
	FifoMeta() {}
	FifoMeta(const uint64_t cluster,
		 const uint64_t localMem,
		 const uint64_t id,
		 const uint64_t size,
		 const uint64_t typeSize)
		: _cluster(cluster),
		  _localMem(localMem),
		  _id(id),
		  _size(size),
		  _typeSize(typeSize)
		{}
	FifoMeta(const uint64_t cluster,
		 const uint64_t localMem,
		 const uint64_t id,
		 const uint64_t size,
		 const uint64_t typeSize,
		 Codelet *producer,
		 Codelet *consumer)
		: _cluster(cluster),
	          _localMem(localMem),	
		  _id(id),
		  _size(size),
		  _typeSize(typeSize),
		  _producer(producer),
		  _consumer(consumer)
		{}
	uint64_t getCluster() const { return _cluster; }
	uint64_t getLocalMem() const { return _localMem; }
	uint64_t getId() const { return _id; }
	uint64_t getSize() const { return _size; }
	uint64_t getTypeSize() const { return _typeSize; }
	Codelet * getProducer() { return _producer; }
	Codelet * getConsumer() { return _consumer; }
	void disassocProd() { _producer = nullptr; }
	void disassocCons() { _consumer = nullptr; }
    };
	

    /**
     * \brief FIFO abstraction for streaming and token passing. Template for polymorphism
     */
    class Fifo {
    protected: //was private -- should this still be private?
        FifoMeta _meta;
    public:
        Fifo() {}
        Fifo(const uint64_t cluster,
             const uint64_t localMem,
             const uint64_t id,
	     uint64_t size,
	     uint64_t typeSize) 
        {
	    _meta = FifoMeta(cluster, localMem, id, size, typeSize);
	}
        Fifo(const uint64_t cluster,
             const uint64_t localMem,
             const uint64_t id,
             uint64_t size,
	     uint64_t typeSize,
             Codelet *producer,
             Codelet *consumer) 
        {
	    _meta = FifoMeta(cluster, localMem, id, size, typeSize, producer, consumer);
	}
	virtual ~Fifo() { } //makes Fifo polymorphic for dynamic_cast

        /** \brief @return the cluster ID */
        uint64_t getCluster()  const { return _meta.getCluster();  }
        /** \brief @return the local memory ID */
        uint64_t getLocalMem() const { return _meta.getLocalMem(); }
        /** \brief @return the core ID */
        uint64_t getId()       const { return _meta.getId();       }

        uint64_t getSize()     const { return _meta.getSize();     }
	uint64_t getTypeSize() const { return _meta.getTypeSize(); }
	FifoMeta * getFifoMeta() const { return (FifoMeta *) &(_meta); }
        //uint64_t getNumConsumers() const { return _meta.; }
	//num consumers can be added later
        Codelet * getProducer()   { return _meta.getProducer(); }
        Codelet * getConsumer()   { return _meta.getConsumer(); } //will have to be extended if multiple consumers allowed
	void disassocProd() { this->_meta.disassocProd(); }
	void disassocCons() { this->_meta.disassocCons(); }

	/* Fifo class can't be a template because Codelets can't return them
	 * without specifying type. Because Fifo can't be a template, these functions
	 * can't be virtual. It is bad practice but all subclasses will have to have
	 * the same pop, push, and peek functions for consistency, like a standard.
        virtual uint64_t push(T toPush) = 0;
	virtual uint64_t pop(T *toPop) = 0;
	virtual uint64_t peek(T *toPeek) = 0;
	*/

        
    };

    // Lockless software Fifo implementation using deque from threadsafe
    template <typename T>
    class SoftFifo: public Fifo {
        private:
	        ringBuffRef<T> _queue;
        public:
            SoftFifo()
            {
            }
            SoftFifo(const uint64_t cluster,
             const uint64_t localMem,
             const uint64_t id,
             const uint64_t size,
             Codelet *producer,
             Codelet *consumer)
            : Fifo(cluster, localMem, id, size, sizeof(T), producer, consumer)
             {
		        _queue.initBuff(size);
             }
            
            
	    uint64_t push(T toPush) {
            // full if only 1 element available to avoid full/empty flags
            /*
            if ((_head == 0 && (_tail == (_meta.size - 1))) || (_tail == _head - 1)) {
                return(-1);
            }
            else {
                _queue[_tail] = toPush;
                _tail = (_tail + 1) % _meta.size; //modulus for ring buffer wrap around
            }
		    else { 
		        _tail++; 
		    }
		    if ((_tail == _head - 1) || ((_tail == (_meta._size - 1)) && (_head == 0))) {
			    // if new _tail 1 behind _head, its full (has to leave empty slot)
			    // also if _tail is last element and _head is 0 (wraparound)
			    _full = true;

		    }
            */
            if (_queue.push(toPush)) {
                return(0);
            }
            else return(-1);
        }

	    uint64_t pop(T *toPop) {
            if(_queue.pull(toPop)) {
                return(0);
            }
            else return(-1);
        }
        
            // can remove locks because not writing anything?
            // gets head of queue but does not remove it / change flags
            //template <typename T> int peek(T *toPeek) {
        /*
	    uint64_t peek(T *toPeek) {
                return(0);
            }

            uint64_t clear() {
                _lock.lock();
                _empty = true;
                _full = false;
                _head = 0;
                _tail = 0;
		        _lock.unlock();
                return(0);
            }
        }
            */

    };


    template <typename T>
    class MsgQFifo: public Fifo {
        private:
            MessageQueue<T> * _msgQ;
        public:
            MsgQFifo()
            {
	        _msgQ = new MessageQueue<T>(1);
            }
	    // size is unused currently
            MsgQFifo(const uint64_t cluster,
             const uint64_t localMem,
             const uint64_t id,
             const uint64_t size,
             Codelet *producer,
             Codelet *consumer)
            : Fifo(cluster, localMem, id, size, sizeof(T), producer, consumer)
             {
		_msgQ = new MessageQueue<T>(1);
	     }
            
            virtual ~MsgQFifo() {
                delete _msgQ;
            }
            
	    uint64_t push(T toPush) {
                //std::cout << "pushing to MsgQ" << std::endl;
                _msgQ->qPut(0, toPush);
                return(0);
            }

	    uint64_t pop(T *toPop) {
                //std::cout << "popping from MsgQ" << std::endl;
                return(_msgQ->qGet(0, toPop));
            }
        
	    uint64_t peek(T *toPeek) {
                return(_msgQ->qPoll(0, toPeek));
            }

	    // no implementation currently
            uint64_t clear() {
                return(0);
            }

    };

    //template <typename T>

} //namespace darts

#endif
