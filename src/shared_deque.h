/*
 * Copyright (C) 2015-2021  Marco Bortolin
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

#ifndef IBMULATOR_SHARED_DEQUE
#define IBMULATOR_SHARED_DEQUE

#include <deque>
#include <mutex>
#include <exception>
#include <condition_variable>

/** Multiple producer, multiple consumer thread safe deque
* Since 'return by reference' is used this deque won't throw */
template<typename T>
class shared_deque
{
	std::deque<T> m_deque;
	mutable std::mutex m_mutex;
	std::condition_variable m_data_cond;

	shared_deque& operator=(const shared_deque&) = delete;
	shared_deque(const shared_deque& other) = delete;

public:

	shared_deque() {}

	void push(T _item)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_deque.push_back(_item);
		}
		m_data_cond.notify_one();
	}

	// return immediately, with true if successful retrieval
	bool try_and_pop(T& _popped_item)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(m_deque.empty()){
			return false;
		}
		_popped_item = std::move(m_deque.front());
		m_deque.pop_front();
		return true;
	}

	// return immediately
	void try_and_pop()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(!m_deque.empty()){
			m_deque.pop_front();
		}
	}

	// return immediately, with true if successful retrieval
	bool try_and_copy(T& _item)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(m_deque.empty()){
			return false;
		}
		_item = std::move(m_deque.front());
		return true;
	}

	// Try to retrieve, if no items, wait till an item is available and try again
	void wait_and_pop(T &_popped_item)
	{
		// note: unique_lock is needed for std::condition_variable::wait
		std::unique_lock<std::mutex> lock(m_mutex);
		while(m_deque.empty()) {
			m_data_cond.wait(lock);
		}
		_popped_item = std::move(m_deque.front());
		m_deque.pop_front();
	}

	bool empty() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_deque.empty();
	}

	unsigned size() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_deque.size();
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_deque.clear();
	}

	size_t acquire_iterator(typename std::deque<T>::iterator &_it)
	{
		m_mutex.lock();
		if(m_deque.empty()) {
			m_mutex.unlock();
			return 0;
		}
		_it = m_deque.begin();
		return m_deque.size();
	}

	void release_iterator()
	{
		m_mutex.unlock();
	}
};

#endif
