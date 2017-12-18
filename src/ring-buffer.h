/* Custom implementation of a circular buffer, which can be used safetly with interrupts
 * Either the interrupts writes and the normal program reads or the other way arround
 * by this it is ensured that the buffer is always in a correct state
 * Hence, we avoid the usage of a counter for the implemetation, since it needs to be
 * changed in both the read and the write method.
 */

#pragma once

template<typename T> class RingBuffer  {
    public:
        RingBuffer(uint16_t size) : size(size)  { //maximal number of elements to store
            startAddr = (T *) malloc(size * sizeof(T)); //addr of first element
            endAddr = startAddr + ((size - 1)* sizeof(T)); //addr of last element
            head = startAddr; 
            tail = startAddr;
        }

        bool write(T *element) volatile  {
            T *nextElement = head + 1;
            if(nextElement > endAddr)
                nextElement = startAddr;
            if(nextElement != tail)  { //that the buffer is not full
                memcpy(head, element, sizeof(T));
                head = nextElement;
                return true;
            }
            else  {
                return false; //write header is faster than read header, since we would lose data
            }
        }

        bool read(T *element) volatile  {
            if(head != tail)  {
                memcpy(element, tail, sizeof(T));
                if(++tail > endAddr)
                    tail = startAddr;
                return true;
            }
            else
                return false;
        }

    private: 
        const uint16_t size; //maximal number of elements
        T *startAddr;
        T *endAddr;
        T *head; //write header
        T *tail; //read header
};
