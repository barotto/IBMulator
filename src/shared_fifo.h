/*
 * Copyright (C) 2023  Marco Bortolin
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

#ifndef IBMULATOR_SHAREDFIFO_H
#define IBMULATOR_SHAREDFIFO_H

#include <queue>
#include <mutex>

/** 
 * Single producer, multiple consumers.
 */
template<typename Element>
class SharedFifo
{
private:
	std::queue<Element> m_data;
	size_t m_max_size = 0;
	mutable std::mutex m_mutex;
	std::condition_variable m_data_cond;

public:
	SharedFifo() {}
	virtual ~SharedFifo() {}

	void set_max_size(size_t _size);
	void wait_for_space(size_t _size);
	bool push(const Element &_item);
	size_t push(const Element *_item, size_t _count);
	bool force_push(const Element &_item);
	bool force_push(const Element *_item, size_t _count);
	bool pop(Element &item_);
	size_t pop(Element *items_, size_t _max_items);
	bool was_empty() const;
	void clear();
};

// consumers
template<typename Element>
void SharedFifo<Element>::set_max_size(size_t _size)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_max_size = _size;
		std::queue<Element>().swap(m_data);
	}
	m_data_cond.notify_one();
}

// producer
template<typename Element>
void SharedFifo<Element>::wait_for_space(size_t _size)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	while(m_max_size && m_data.size() > m_max_size - _size) {
		m_data_cond.wait(lock);
	}
}

// producer
template<typename Element>
bool SharedFifo<Element>::push(const Element &_item)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if(!m_max_size || m_data.size() < m_max_size) {
		m_data.push(_item);
		// returns if _item has been pushed
		return true;
	}
	return false;
}

// producer
template<typename Element>
size_t SharedFifo<Element>::push(const Element *_item, size_t _count)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	size_t count = 0;
	while(count < _count) {
		if(!m_max_size || m_data.size() < m_max_size) {
			m_data.push(*_item);
			count++;
			_item++;
		} else {
			return count;
		}
	}
	return count;
}

// producer
template<typename Element>
bool SharedFifo<Element>::force_push(const Element &_item)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	bool is_full = (m_max_size && m_data.size() >= m_max_size);
	if(is_full) {
		m_data.pop();
	}
	m_data.push(_item);

	// returns true if successful, false if an overflow occurred
	return !is_full;
}

// producer
template<typename Element>
bool SharedFifo<Element>::force_push(const Element *_item, size_t _count)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	bool is_full = false;
	while(_count--) {
		is_full = (m_max_size && m_data.size() >= m_max_size);
		if(is_full) {
			m_data.pop();
		}
		m_data.push(*_item);
		_item++;
	}
	// returns true if successful, false if an overflow occurred
	return !is_full;
}

// consumers
template<typename Element>
bool SharedFifo<Element>::pop(Element &item_)
{
	bool result = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(!m_data.empty()) {
			item_ = m_data.front();
			m_data.pop();
			result = true;
		}
	}
	m_data_cond.notify_one();
	// returns if item_ is valid
	return result;
}

// consumers
template<typename Element>
size_t SharedFifo<Element>::pop(Element *items_, size_t _max_items)
{
	size_t total = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for(; !m_data.empty() && total < _max_items; total++) {
			items_[total] = m_data.front();
			m_data.pop();
		}
	}
	m_data_cond.notify_one();

	return total;
}

// consumers
template<typename Element>
void SharedFifo<Element>::clear()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::queue<Element>().swap(m_data);
	}
	m_data_cond.notify_one();
}

// consumers
template<typename Element>
bool SharedFifo<Element>::was_empty() const
{
	std::lock_guard<std::mutex> lock(m_mutex);

	return m_data.empty();
}

#endif
