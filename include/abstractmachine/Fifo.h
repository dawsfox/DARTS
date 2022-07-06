
#ifndef DARTS_HWLOC_FIFO_H
#define DARTS_HWLOC_FIFO_H

#include "Unit.h"
#include <vector>
#include "Lock.h"

namespace darts {
namespace hwloc {
	
    class FifoMeta {
    public:
        uint64_t _cluster,
		 _localMem,
		 _id,
		 _size,
		 _typeSize;
	Unit * _producer,
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
		 Unit *producer,
		 Unit *consumer)
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
	Unit * getProducer() const { return _producer; }
	Unit * getConsumer() const { return _consumer; }
    };
	

    /**
     * \brief FIFO abstraction for streaming and token passing
     */
    class Fifo {
    protected: //was private -- should this still be private?
	    /*
        uint64_t _cluster, 
                 _localMem,
                 _id,
                 _size, //size in number of elements
                 _numConsumers; //may or may not be removed later
        Unit * _producer, //should be Unit or codelet? Unit would allow it to persist which may be better
             * _consumer;
	    */
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
             Unit *producer,
             Unit *consumer) 
		/*
        : _cluster(cluster),
          _localMem(localMem),
          _id(id),
          _size(size),
          _producer(producer),
          _consumer(consumer)
	        */
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
        //uint64_t getNumConsumers() const { return _meta.; }
	//num consumers can be added later
        Unit * getProducer()   const { return _meta.getProducer(); }
        Unit * getConsumer()   const { return _meta.getConsumer(); } //will have to be extended if multiple consumers allowed

        //template <typename T> uint64_t push(T toPush) {
        //}
        // init is part of constructor

        
    };

    template <typename T>
    class SoftFifo: protected Fifo {
        private:
            uint32_t _head,
                     _tail;
            bool _full;
            bool _empty;
            //char* _queue;
	    // void* _queue2;
	    std::vector<T> _queue; //how to have it typeless? void pointer? this points to vector
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
             Unit *producer,
             Unit *consumer)
            : Fifo(cluster, localMem, id, size, sizeof(T), producer, consumer)
             {
                _head = 0;
                _tail = 0;
                _full = false;
                _empty = true;
		_queue = new std::vector<T>();
                //_queue = new char[_size]; //should I use templates for the queue? So that typing can be dynamic
                //also should I use auto and std::vector? maybe get rid of this ...
             }
            
            ~SoftFifo() {
                delete[] _queue;
            }
            
	    /*
            template <typename T> int initQueue() {
                _queue2 = (std::vector<T> *) new std::vector<T> (_size);
                return(0);
            }
	    int initQueue() {
		_queue2 = (std::vector<T> *) new std::vector<T> (_size);
		return(0);
	    }

            int destroyQueue() {
                delete[] _queue2;
                return(0);
            }
	    */

            //template <typename T> int push(T toPush) {
	    int push(T toPush) {
                _lock.lock();
                if (!_full) {
                    if (_empty) {
                        _empty = false; //pushing so not empty anymore
                    }
                    _queue[_tail] = toPush;
                    _tail++;
                    if (_tail == _head) {
                        _full = true; //if head and tail equal and not empty, must be full
                    }
                }
                else {
                    _lock.unlock();
                    return(-1); //-1 --> push failed, FIFO full
                }
                _lock.unlock();
                return(0);
            }

            //template <typename T> int pop(T *toPop) {
	    int pop(T *toPop) {
                _lock.lock();
                if (!_empty) {
                    if (_full) {
                        _full = false;
                    }
                    *toPop = _queue[_head]; //copy to arg
                    _head++;
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
	    int peek(T *toPeek) {
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

            int clear() {
                _lock.lock();
                _empty = true;
                _full = false;
                _head = 0;
                _tail = 0;
                return(0);
            }

    };
}
}

#endif
