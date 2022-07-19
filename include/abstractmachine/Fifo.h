
#ifndef DARTS_HWLOC_FIFO_H
#define DARTS_HWLOC_FIFO_H

#include <vector>
#include "Lock.h"
#include "Codelet.h"
#include "MsgQ.hpp"

namespace darts {
//namespace hwloc {
	
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
	Codelet * getProducer() const { return _producer; }
	Codelet * getConsumer() const { return _consumer; }
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
        Codelet * getProducer()   const { return _meta.getProducer(); }
        Codelet * getConsumer()   const { return _meta.getConsumer(); } //will have to be extended if multiple consumers allowed

	/* Fifo class can't be a template because Codelets can't return them
	 * without specifying type. Because Fifo can't be a template, these functions
	 * can't be vulnerable. It is bad practice but all subclasses will have to have
	 * the same pop, push, and peek functions for consistency, like a standard.
        virtual uint64_t push(T toPush) = 0;
	virtual uint64_t pop(T *toPop) = 0;
	virtual uint64_t peek(T *toPeek) = 0;
	*/

        
    };

    //should just replace this with MsgQ asap
    //also this queue could be lockless with an empty element
    template <typename T>
    class SoftFifo: protected Fifo {
        private:
            uint32_t _head,
                     _tail;
            bool _full;
            bool _empty;
	    std::vector<T> * _queue;
            Lock _lock;
        public:
            SoftFifo()
            {
                _head = 0;
                _tail = 0;
                _full = false;
                _empty = true;
            }
            SoftFifo(const uint64_t cluster,
             const uint64_t localMem,
             const uint64_t id,
             const uint64_t size,
             Codelet *producer,
             Codelet *consumer)
            : Fifo(cluster, localMem, id, size, sizeof(T), producer, consumer)
             {
                _head = 0;
                _tail = 0;
                _full = false;
                _empty = true;
		_queue = new std::vector<T>(size);
             }
            
            ~SoftFifo() {
                delete[] _queue;
            }
            
            //template <typename T> int push(T toPush) {
	    uint64_t push(T toPush) {
                _lock.lock();
                if (!_full) {
                    if (_empty) {
                        _empty = false; //pushing so not empty anymore
                    }
                    _queue[_tail] = toPush;
		    if (_tail + 1 > _meta._size - 1) { // wrap around on tail
			_tail = 0;
		    }
		    else { 
		        _tail++; 
		    }
		    if ((_tail == _head - 1) || ((_tail == (_meta._size - 1)) && (_head == 0))) {
			    // if new _tail 1 behind _head, its full (has to leave empty slot)
			    // also if _tail is last element and _head is 0 (wraparound)
			    _full = true;

		    }
                    
		    /*
                    if (_tail == _head) {
                        _full = true; //if head and tail equal and not empty, must be full
                    }
		    */
                }
                else {
                    _lock.unlock();
                    return(-1); //-1 --> push failed, FIFO full
                }
                _lock.unlock();
                return(0);
            }

	    uint64_t pop(T *toPop) {
                _lock.lock();
                if (!_empty) {
                    if (_full) {
                        _full = false;
                    }
                    *toPop = _queue[_head]; //copy to arg
                    _head++;
		    if (_head == _meta._size) { //wraparound
		        _head = 0;
		    }
                    if (_head == _tail) {
                        _empty = true;
                    }
                }
                else {
                    // could add spin here for blocking
                    _lock.unlock();
                    return(-1);
                }
                _lock.unlock();
                return(0);
            }
        
            // can remove locks because not writing anything?
            // gets head of queue but does not remove it / change flags
            //template <typename T> int peek(T *toPeek) {
	    uint64_t peek(T *toPeek) {
                _lock.lock();
                if (!_empty) {
                    *toPeek = _queue[_head]; //copy to arg
                }
                else {
                    // could add spin here for blocking
                    _lock.unlock();
                    return(-1);
                }
                _lock.unlock();
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

    };


    template <typename T>
    class MsgQFifo: protected Fifo {
        private:
            MessageQueue<T> * _msgQ;
        public:
            MsgQFifo()
            {
	        _msgQ = new MessageQueue<T>;
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
		_msgQ = new MessageQueue<T>;
	     }
            
            ~MsgQFifo() {
                delete _msgQ;
            }
            
	    uint64_t push(T toPush) {
                _msgQ->qPut(0, toPush);
                return(0);
            }

	    uint64_t pop(T *toPop) {
                return(_msgQ->qGet(0, toPop));
            }
        
	    uint64_t peek(T *toPeek) {
                return(_msgQ->qPoll(0, toPeek));
            }

            uint64_t clear() {
                return(0);
            }

    };

//}
} //namespace darts

#endif
