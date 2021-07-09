/*
* Not any company's property but Public-Domain
* Do with source-code as you will. No requirement to keep this
* header if need to use it/change it/ or do whatever with it
*/

// Single user, single consumer, circular FIFO
// http://www.codeproject.com/Articles/43510/Lock-Free-Single-Producer-Single-Consumer-Circular


#ifndef IBMULATOR_CIRCULARFIFO_H
#define IBMULATOR_CIRCULARFIFO_H

#include <atomic>
#include <cstddef>

template<typename Element, size_t Size>
class circular_fifo
{
public:
	enum { Capacity = Size+1 };

private:
	Element m_array[Capacity];
	std::atomic<size_t> m_tail = 0; // tail(input) index
	std::atomic<size_t> m_head = 0; // head(output) index

public:
	circular_fifo() {}
	virtual ~circular_fifo() {}

	bool push(const Element &_item); // TODO pushByMOve?
	bool pop(Element &item_);
	void clear();

	bool was_empty() const;
	bool was_full() const;
	bool is_lock_free() const;

private:
	size_t increment(size_t idx) const;
};



// Push by producer.
template<typename Element, size_t Size>
bool circular_fifo<Element, Size>::push(const Element &_item)
{
	// memory_order_seq_cst for every operation.
	// Tail is only changed by producer and can be safely loaded using memory_order_relexed
	// Head is updated by consumer and must be loaded using at least memory_order_acquire
	const auto current_tail = m_tail.load();
	const auto next_tail = increment(current_tail);
	if(next_tail != m_head.load()) {
		m_array[current_tail] = _item;
		m_tail.store(next_tail);
		return true;
	}
	return false;  // full queue
}

// Pop by consumer
template<typename Element, size_t Size>
bool circular_fifo<Element, Size>::pop(Element &item_)
{
	// can only update the head
	const auto current_head = m_head.load();
	if(current_head == m_tail.load()) {
		return false; // empty queue
	}

	item_ = m_array[current_head];
	m_head.store(increment(current_head));
	return true;
}

// Clear by consumer
template<typename Element, size_t Size>
void circular_fifo<Element, Size>::clear()
{
	m_head.store(m_tail.load());
}

// Snapshot with acceptance of that this comparison function is not atomic
// Used by clients or test, since pop() avoid double load overhead by not
// using was_empty()
template<typename Element, size_t Size>
bool circular_fifo<Element, Size>::was_empty() const
{
	return (m_head.load() == m_tail.load());
}

// Snapshot with acceptance that this comparison is not atomic
// Used by clients or test, since push() avoid double load overhead by not
// using was_full()
template<typename Element, size_t Size>
bool circular_fifo<Element, Size>::was_full() const
{
	const auto next_tail = increment(m_tail.load());
	return (next_tail == m_head.load());
}

template<typename Element, size_t Size>
bool circular_fifo<Element, Size>::is_lock_free() const
{
	return (m_tail.is_lock_free() && m_head.is_lock_free());
}

template<typename Element, size_t Size>
size_t circular_fifo<Element, Size>::increment(size_t idx) const
{
	return (idx + 1) % Capacity;
}



#endif
