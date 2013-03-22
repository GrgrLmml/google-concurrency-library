// Copyright 2010 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BUFFER_QUEUE_H
#define BUFFER_QUEUE_H

#include "mutex.h"
#include "condition_variable.h"

#include "queue_base.h"

namespace gcl {

template <typename Value>
class buffer_queue
{
  public:
    typedef Value value_type;

    buffer_queue() CXX11_DELETED
    buffer_queue(const buffer_queue&) CXX11_DELETED
    buffer_queue(size_t max_elems, const char* name);
    explicit buffer_queue(size_t max_elems);
    template <typename Iter>
    buffer_queue(size_t max_elems, Iter first, Iter last, const char* name);
    template <typename Iter>
    buffer_queue(size_t max_elems, Iter first, Iter last);
    buffer_queue& operator =(const buffer_queue&) CXX11_DELETED
    ~buffer_queue();

//TODO(crowl): Do we want this?
#if 0
    generic_queue_back<value_type> back()
        { return generic_queue_back<value_type>(this); }
    generic_queue_front<value_type> front()
        { return generic_queue_front<value_type>(this); }
#endif

    void close();
    bool is_closed();
    bool is_empty();

    Value value_pop();
    queue_op_status wait_pop(Value&);
    queue_op_status try_pop(Value&);
    queue_op_status nonblocking_pop(Value&);

    void push(const Value& x);
    queue_op_status wait_push(const Value& x);
    queue_op_status try_push(const Value& x);
    queue_op_status nonblocking_push(const Value& x);
#ifdef HAS_CXX11_RVREF
    void push(Value&& x);
    queue_op_status wait_push(Value&& x);
    queue_op_status try_push(Value&& x);
    queue_op_status nonblocking_push(Value&& x);
#endif

    const char* name();

  private:
    mutex mtx_;
    condition_variable not_empty_;
    condition_variable not_full_;
    size_t waiting_full_;
    size_t waiting_empty_;
    Value* buffer_;
    size_t push_index_;
    size_t pop_index_;
    size_t num_slots_;
    bool closed_;
    const char* name_;

    void init(size_t max_elems);

    template <typename Iter>
    void iter_init(size_t max_elems, Iter first, Iter last);

    size_t next(size_t idx) { return (idx + 1) % num_slots_; }

    queue_op_status try_pop_common(Value& x);
    queue_op_status try_push_common(const Value& x);
#ifdef HAS_CXX11_RVREF
    queue_op_status try_push_common(Value&& x);
#endif

    queue_op_status pop_from(Value& elem, size_t pdx)
    {
        pop_index_ = next( pdx );
        if ( waiting_full_ > 0 ) {
            --waiting_full_;
            not_full_.notify_one();
        }
        // The change to the queue must happen before the copy/move
        // has a chance to fail.
#ifdef HAS_CXX11_RVREF
        elem = std::move(buffer_[pdx]);
#else
        elem = buffer_[pdx];
#endif
        return CXX11_ENUM_QUAL(queue_op_status)success;
    }

    void push_reindex( size_t nxt )
    {
        push_index_ = nxt;
        if ( waiting_empty_ > 0 ) {
            --waiting_empty_;
            not_empty_.notify_one();
        }
    }

    queue_op_status push_at(const Value& elem, size_t hdx, size_t nxt)
    {
        buffer_[hdx] = elem;
        // The change to the queue must happen only after the copy succeeds.
        push_reindex( nxt );
        return CXX11_ENUM_QUAL(queue_op_status)success;
    }

#ifdef HAS_CXX11_RVREF
    queue_op_status push_at(Value&& elem, size_t hdx, size_t nxt)
    {
        buffer_[hdx] = std::move(elem);
        // The change to the queue must happen only after the copy succeeds.
        push_reindex( nxt );
        return CXX11_ENUM_QUAL(queue_op_status)success;
    }
#endif

};

template <typename Value>
void buffer_queue<Value>::init(size_t max_elems)
{
    if ( max_elems < 1 )
        throw std::invalid_argument("number of elements must be at least one");
}

template <typename Value>
buffer_queue<Value>::buffer_queue(size_t max_elems, const char* name)
:
    waiting_full_( 0 ),
    waiting_empty_( 0 ),
    buffer_( new Value[max_elems+1] ),
    push_index_( 0 ),
    pop_index_( 0 ),
    num_slots_( max_elems+1 ),
    closed_( false ),
    name_( name )
{
    init(max_elems);
}

template <typename Value>
buffer_queue<Value>::buffer_queue(size_t max_elems)
:
    // would rather do buffer_queue(max_elems, "")
    waiting_full_( 0 ),
    waiting_empty_( 0 ),
    buffer_( new Value[max_elems+1] ),
    push_index_( 0 ),
    pop_index_( 0 ),
    num_slots_( max_elems+1 ),
    closed_( false ),
    name_( "" )
{
    init(max_elems);
}

template <typename Value>
template <typename Iter>
void buffer_queue<Value>::iter_init(size_t max_elems, Iter first, Iter last)
{
    size_t hdx = 0;
    for ( Iter cur = first; cur != last; ++cur ) {
        if ( hdx >= max_elems )
            throw std::invalid_argument("too few slots for iterator");
        buffer_[hdx] = *cur;
        hdx += 1; // more efficient than next(hdx)
    }
    push_reindex( hdx );
}

template <typename Value>
template <typename Iter>
buffer_queue<Value>::buffer_queue(size_t max_elems, Iter first, Iter last,
                                    const char* name)
:
    // would rather do buffer_queue(max_elems, name)
    waiting_full_( 0 ),
    waiting_empty_( 0 ),
    buffer_( new Value[max_elems+1] ),
    push_index_( 0 ),
    pop_index_( 0 ),
    num_slots_( max_elems+1 ),
    closed_( false ),
    name_( name )
{
    iter_init(max_elems, first, last);
}

template <typename Value>
template <typename Iter>
buffer_queue<Value>::buffer_queue(size_t max_elems, Iter first, Iter last)
:
    // would rather do buffer_queue(max_elems, first, last, "")
    waiting_full_( 0 ),
    waiting_empty_( 0 ),
    buffer_( new Value[max_elems+1] ),
    push_index_( 0 ),
    pop_index_( 0 ),
    num_slots_( max_elems+1 ),
    closed_( false ),
    name_( "" )
{
    iter_init(max_elems, first, last);
}

template <typename Value>
buffer_queue<Value>::~buffer_queue()
{
    delete[] buffer_;
}

template <typename Value>
void buffer_queue<Value>::close()
{
    lock_guard<mutex> hold( mtx_ );
    closed_ = true;
    not_empty_.notify_all();
    not_full_.notify_all();
}

template <typename Value>
bool buffer_queue<Value>::is_closed()
{
    lock_guard<mutex> hold( mtx_ );
    return closed_;
}

template <typename Value>
bool buffer_queue<Value>::is_empty()
{
    lock_guard<mutex> hold( mtx_ );
    return push_index_ == pop_index_;
}

template <typename Value>
queue_op_status buffer_queue<Value>::try_pop_common(Value& elem)
{
    size_t pdx = pop_index_;
    if ( pdx == push_index_ ) {
        if ( closed_ )
            return CXX11_ENUM_QUAL(queue_op_status)closed;
        else
            return CXX11_ENUM_QUAL(queue_op_status)empty;
    }
    return pop_from( elem, pdx );
}

template <typename Value>
queue_op_status buffer_queue<Value>::try_pop(Value& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment operator
       in the pop_from operation. */
    try {
        lock_guard<mutex> hold( mtx_ );
        return try_pop_common(elem);
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
queue_op_status buffer_queue<Value>::nonblocking_pop(Value& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment operator
       in the pop_from operation. */
    try {
        unique_lock<mutex> hold( mtx_, try_to_lock );
        if ( !hold.owns_lock() ) {
            return CXX11_ENUM_QUAL(queue_op_status)busy;
        }
        return try_pop_common(elem);
    } catch (...) {
        close();
        throw;
    }
}
template <typename Value>
queue_op_status buffer_queue<Value>::wait_pop(Value& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment operator
       in the pop_from operation. */
    try {
        unique_lock<mutex> hold( mtx_ );
        size_t pdx;
        for (;;) {
            pdx = pop_index_;
            if ( pdx != push_index_ )
                break;
            if ( closed_ )
                return CXX11_ENUM_QUAL(queue_op_status)closed;
            ++waiting_empty_;
            not_empty_.wait( hold );
        }
        return pop_from( elem, pdx );
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
Value buffer_queue<Value>::value_pop()
{
    /* This try block is here to catch exceptions from the
       user-defined copy assignment operator. */
    try {
        Value elem;
        if ( wait_pop( elem ) == CXX11_ENUM_QUAL(queue_op_status)closed )
            throw CXX11_ENUM_QUAL(queue_op_status)closed;
#ifdef HAS_CXX11_RVREF
        return std::move(elem);
#else
        return elem;
#endif
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
queue_op_status buffer_queue<Value>::try_push_common(const Value& elem)
{
    if ( closed_ )
        return CXX11_ENUM_QUAL(queue_op_status)closed;
    size_t hdx = push_index_;
    size_t nxt = next( hdx );
    if ( nxt == pop_index_ )
        return CXX11_ENUM_QUAL(queue_op_status)full;
    return push_at( elem, hdx, nxt );
}

template <typename Value>
queue_op_status buffer_queue<Value>::try_push(const Value& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment
       operator in push_at. */
    try {
        lock_guard<mutex> hold( mtx_ );
        return try_push_common(elem);
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
queue_op_status buffer_queue<Value>::nonblocking_push(const Value& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment
       operator in push_at. */
    try {
        unique_lock<mutex> hold( mtx_, try_to_lock );
        if ( !hold.owns_lock() )
            return CXX11_ENUM_QUAL(queue_op_status)busy;
        return try_push_common(elem);
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
queue_op_status buffer_queue<Value>::wait_push(const Value& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment
       operator in push_at. */
    try {
        unique_lock<mutex> hold( mtx_ );
        size_t hdx;
        size_t nxt;
        for (;;) {
            if ( closed_ )
                return CXX11_ENUM_QUAL(queue_op_status)closed;
            hdx = push_index_;
            nxt = next( hdx );
            if ( nxt != pop_index_ )
                break;
            ++waiting_full_;
            not_full_.wait( hold );
        }
        return push_at( elem, hdx, nxt );
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
void buffer_queue<Value>::push(const Value& elem)
{
    /* Only wait_push can throw, and it protects itself, so there
       is no need to try/catch here. */
    if ( wait_push( elem ) == CXX11_ENUM_QUAL(queue_op_status)closed ) {
        throw CXX11_ENUM_QUAL(queue_op_status)closed;
    }
}

#ifdef HAS_CXX11_RVREF

//TODO(crowl) Refactor with non-move versions.

template <typename Value>
queue_op_status buffer_queue<Value>::try_push_common(Value&& elem)
{
    if ( closed_ )
        return CXX11_ENUM_QUAL(queue_op_status)closed;
    size_t hdx = push_index_;
    size_t nxt = next( hdx );
    if ( nxt == pop_index_ )
        return CXX11_ENUM_QUAL(queue_op_status)full;
    return push_at( std::move(elem), hdx, nxt );
}


template <typename Value>
queue_op_status buffer_queue<Value>::try_push(Value&& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment
       operator in push_at. */
    try {
        lock_guard<mutex> hold( mtx_ );
        return try_push_common(elem);
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
queue_op_status buffer_queue<Value>::nonblocking_push(Value&& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment
       operator in push_at. */
    try {
        unique_lock<mutex> hold( mtx_, try_to_lock );
        if (!hold.owns_lock()) {
            return CXX11_ENUM_QUAL(queue_op_status)busy;
        }
        return try_push_common(elem);
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
queue_op_status buffer_queue<Value>::wait_push(Value&& elem)
{
    /* This try block is here to catch exceptions from the mutex
       operations or from the user-defined copy assignment
       operator in push_at. */
    try {
        unique_lock<mutex> hold( mtx_ );
        size_t hdx;
        size_t nxt;
        for (;;) {
            if ( closed_ )
                return CXX11_ENUM_QUAL(queue_op_status)closed;
            hdx = push_index_;
            nxt = next( hdx );
            if ( nxt != pop_index_ )
                break;
            ++waiting_full_;
            not_full_.wait( hold );
        }
        return push_at( std::move(elem), hdx, nxt );
    } catch (...) {
        close();
        throw;
    }
}

template <typename Value>
void buffer_queue<Value>::push(Value&& elem)
{
    /* Only wait_push can throw, and it protects itself, so there
       is no need to try/catch here. */
    if ( wait_push( std::move(elem) )
         == CXX11_ENUM_QUAL(queue_op_status)closed )
        throw CXX11_ENUM_QUAL(queue_op_status)closed;
}

#endif

template <typename Value>
const char* buffer_queue<Value>::name()
{
    return name_;
}

} // namespace gcl

#endif
