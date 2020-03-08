/*
 * Copyright (C) 2015-2020  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

/** ==========================================================================
* 2010 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
* with no warranties. This code is yours to share, use and modify with no
* strings attached and no restrictions or obligations.
* ============================================================================
*
* Example of a normal std::queue protected by a mutex for operations,
* making it safe for thread communication, using std::mutex from C++0x with
* the help from the std::thread library from JustSoftwareSolutions
* ref: http://www.stdthread.co.uk/doc/headers/mutex.html
*
* This example was totally inspired by Anthony Williams lock-based data structures in
* Ref: "C++ Concurrency In Action" http://www.manning.com/williams */

#ifndef IBMULATOR_SHARED_QUEUE
#define IBMULATOR_SHARED_QUEUE

#include <queue>
#include <mutex>
#include <exception>
#include <condition_variable>

/** Multiple producer, multiple consumer thread safe queue
* Since 'return by reference' is used this queue won't throw */
template<typename T>
class shared_queue
{
	std::queue<T> queue_;
	mutable std::mutex m_;
	std::condition_variable data_cond_;

	shared_queue& operator=(const shared_queue&) = delete;
	shared_queue(const shared_queue& other) = delete;

public:

	shared_queue() {}

	void push(T&& item)
	{
		{
			std::lock_guard<std::mutex> lock(m_);
			queue_.push(std::forward<T>(item));
		}
		data_cond_.notify_one();
	}

	// return immediately, with true if successful retrieval
	bool try_and_pop(T& popped_item)
	{
		std::lock_guard<std::mutex> lock(m_);
		if(queue_.empty()){
			return false;
		}
		popped_item = std::move(queue_.front());
		queue_.pop();
		return true;
	}

	// return immediately
	void try_and_pop()
	{
		std::lock_guard<std::mutex> lock(m_);
		if(!queue_.empty()){
			queue_.pop();
		}
		return;
	}

	// return immediately, with true if successful retrieval
	bool try_and_copy(T& _item)
	{
		std::lock_guard<std::mutex> lock(m_);
		if(queue_.empty()){
			return false;
		}
		_item = std::move(queue_.front());
		return true;
	}

	// Try to retrieve, if no items, wait till an item is available and try again
	void wait_and_pop(T& popped_item)
	{
		// note: unique_lock is needed for std::condition_variable::wait
		std::unique_lock<std::mutex> lock(m_);
		while(queue_.empty()) {
			//The 'while' loop is equal to
			//data_cond_.wait(lock, [](bool result){return !queue_.empty();});
			data_cond_.wait(lock);
		}
		popped_item=std::move(queue_.front());
		queue_.pop();
	}

	// Try to retrieve, if no items wait till an item is available or the timeout
	// has expired. if return value is  std::cv_status::timeout then no value was popped. 
	std::cv_status wait_for_and_pop(T& popped_item, unsigned _max_wait_ns)
	{
		std::unique_lock<std::mutex> lock(m_);
		std::cv_status event = std::cv_status::no_timeout;
		while(queue_.empty()) {
			event = data_cond_.wait_for(lock, std::chrono::nanoseconds(_max_wait_ns));
			if(event == std::cv_status::timeout) {
				return event;
			}
		}
		popped_item = std::move(queue_.front());
		queue_.pop();
		return event;
	}
	
	bool empty() const
	{
		std::lock_guard<std::mutex> lock(m_);
		return queue_.empty();
	}

	unsigned size() const
	{
		std::lock_guard<std::mutex> lock(m_);
		return queue_.size();
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(m_);
		std::queue<T>().swap(queue_);
	}
};

#endif
