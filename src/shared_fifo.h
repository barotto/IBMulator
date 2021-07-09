/*
 * Copyright (C) 2021  Marco Bortolin
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

template<typename Element, size_t Size>
class SharedFifo
{
private:
	std::queue<Element> m_data;
	mutable std::mutex m_mutex;
	
public:
	SharedFifo() {}
	virtual ~SharedFifo() {}

	bool push(const Element &_item);
	bool force_push(const Element &_item);
	bool pop(Element &item_);
	void clear();
};

template<typename Element, size_t Size>
bool SharedFifo<Element, Size>::push(const Element &_item)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if(m_data.size() < Size) {
		m_data.push(_item);
		return true;
	}
	return false;
}

template<typename Element, size_t Size>
bool SharedFifo<Element, Size>::force_push(const Element &_item)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	bool is_full = m_data.size() >= Size;
	if(is_full) {
		m_data.pop();
	}
	m_data.push(_item);
	return !is_full;
}

template<typename Element, size_t Size>
bool SharedFifo<Element, Size>::pop(Element &item_)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if(m_data.empty()) {
		return false;
	}
	item_ = m_data.front();
	m_data.pop();
	return true;
}

template<typename Element, size_t Size>
void SharedFifo<Element, Size>::clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::queue<Element>().swap(m_data);
}



#endif
