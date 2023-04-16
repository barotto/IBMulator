/*
Copyright (c) 2011 Erik Garrison

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
/*
https://github.com/ekg/intervaltree
Overview:

An interval tree can be used to efficiently find a set of numeric
intervals overlapping or containing another interval.

This library provides a basic implementation of an interval tree using
C++ templates, allowing the insertion of arbitrary types into the
tree.


Usage:

Add #include "IntervalTree.h" to the source files in which you will
use the interval tree.

To make an IntervalTree to contain objects of class T, use:

   vector<Interval<T> > intervals;
   T a, b, c;
   intervals.push_back(Interval<T>(2, 10, a));
   intervals.push_back(Interval<T>(3, 4, b));
   intervals.push_back(Interval<T>(20, 100, c));
   IntervalTree<T> tree;
   tree = IntervalTree<T>(intervals);

Now, it's possible to query the tree and obtain a set of intervals
which are contained within the start and stop coordinates.

    vector<Interval<T> > results;
    tree.findContained(start, stop, results);
    cout << "found " << results.size()
         << " overlapping intervals" << endl;

The function IntervalTree::findOverlapping provides a method to find
all those intervals which are contained or partially overlap the
interval (start, stop).


Author: Erik Garrison <erik.garrison@gmail.com>

License: MIT
*/
#ifndef IBMULATOR_INTERVAL_TREE_H
#define IBMULATOR_INTERVAL_TREE_H

#include <vector>
#include <algorithm>
#include <iostream>


template <class T, typename K = int>
class Interval {
public:
    K start;
    K stop;
    T value;
    Interval(K s, K e, const T& v)
        : start(s)
        , stop(e)
        , value(v)
    { }
};

template <class T, typename K>
int intervalStart(const Interval<T,K>& i) {
    return i.start;
}

template <class T, typename K>
int intervalStop(const Interval<T,K>& i) {
    return i.stop;
}

template <class T, typename K>
  std::ostream& operator<<(std::ostream& out, Interval<T,K>& i) {
    out << "Interval(" << i.start << ", " << i.stop << "): " << i.value;
    return out;
}

template <class T, typename K = int>
class IntervalStartSorter {
public:
    bool operator() (const Interval<T,K>& a, const Interval<T,K>& b) {
        return a.start < b.start;
    }
};

template <class T, typename K = int>
class IntervalTree {

public:
    typedef Interval<T,K> interval;
    typedef std::vector<interval> intervalVector;
    typedef IntervalTree<T,K> intervalTree;

    intervalVector intervals;
    intervalTree* left;
    intervalTree* right;
    int center;

    IntervalTree<T,K>(void)
        : left(nullptr)
        , right(nullptr)
        , center(0)
    { }

    IntervalTree<T,K>(const intervalTree& other)
        : left(nullptr)
        , right(nullptr)
    {
        center = other.center;
        intervals = other.intervals;
        if (other.left) {
            left = new intervalTree(*other.left);
        }
        if (other.right) {
            right = new intervalTree(*other.right);
        }
    }

    IntervalTree<T,K>& operator=(const intervalTree& other) {
        center = other.center;
        intervals = other.intervals;
        if (other.left) {
            left = new intervalTree(*other.left);
        } else {
            if (left) delete left;
            left = nullptr;
        }
        if (other.right) {
            right = new intervalTree(*other.right);
        } else {
            if (right) delete right;
            right = nullptr;
        }
        return *this;
    }

    IntervalTree<T,K>(
            intervalVector& ivals,
            unsigned int depth = 16,
            unsigned int minbucket = 64,
            int leftextent = 0,
            int rightextent = 0,
            unsigned int maxbucket = 512
            )
        : left(nullptr)
        , right(nullptr)
    {

        --depth;
        IntervalStartSorter<T,K> intervalStartSorter;
        if (depth == 0 || (ivals.size() < minbucket && ivals.size() < maxbucket)) {
            std::sort(ivals.begin(), ivals.end(), intervalStartSorter);
            intervals = ivals;
            center = 0;
        } else {
            if (leftextent == 0 && rightextent == 0) {
                // sort intervals by start
              std::sort(ivals.begin(), ivals.end(), intervalStartSorter);
            }

            int leftp = 0;
            int rightp = 0;
            int centerp = 0;

            if (leftextent || rightextent) {
                leftp = leftextent;
                rightp = rightextent;
            } else {
                leftp = ivals.front().start;
                std::vector<K> stops;
                stops.resize(ivals.size());
                transform(ivals.begin(), ivals.end(), stops.begin(), intervalStop<T,K>);
                rightp = *max_element(stops.begin(), stops.end());
            }

            //centerp = ( leftp + rightp ) / 2;
            centerp = ivals.at(ivals.size() / 2).start;
            center = centerp;

            intervalVector lefts;
            intervalVector rights;

            for (typename intervalVector::iterator i = ivals.begin(); i != ivals.end(); ++i) {
                interval& interval = *i;
                if (interval.stop < center) {
                    lefts.push_back(interval);
                } else if (interval.start > center) {
                    rights.push_back(interval);
                } else {
                    intervals.push_back(interval);
                }
            }

            if (!lefts.empty()) {
                left = new intervalTree(lefts, depth, minbucket, leftp, centerp);
            }
            if (!rights.empty()) {
                right = new intervalTree(rights, depth, minbucket, centerp, rightp);
            }
        }
    }

    void findOverlapping(K start, K stop, intervalVector& overlapping) const {
        if (!intervals.empty() && ! (stop < intervals.front().start)) {
            for (typename intervalVector::const_iterator i = intervals.begin(); i != intervals.end(); ++i) {
                const interval& interval = *i;
                if (interval.stop >= start && interval.start <= stop) {
                    overlapping.push_back(interval);
                }
            }
        }

        if (left && start <= center) {
            left->findOverlapping(start, stop, overlapping);
        }

        if (right && stop >= center) {
            right->findOverlapping(start, stop, overlapping);
        }

    }

    void findContained(K start, K stop, intervalVector& contained) const {
        if (!intervals.empty() && ! (stop < intervals.front().start)) {
            for (typename intervalVector::const_iterator i = intervals.begin(); i != intervals.end(); ++i) {
                const interval& interval = *i;
                if (interval.start >= start && interval.stop <= stop) {
                    contained.push_back(interval);
                }
            }
        }

        if (left && start <= center) {
            left->findContained(start, stop, contained);
        }

        if (right && stop >= center) {
            right->findContained(start, stop, contained);
        }

    }

    ~IntervalTree(void) {
        // traverse the left and right
        // delete them all the way down
        if (left) {
            delete left;
        }
        if (right) {
            delete right;
        }
    }

};

#endif
