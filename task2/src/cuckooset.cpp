#include "cuckooset.hpp"

#include <iostream>

#include "hash.hpp"

#define INITIAL_CAPACITY (1024)
#define RELOCATE_LIMIT (512)

template <class Pheet, typename TT, class Comparator>
CuckooSet<Pheet, TT, Comparator>::CuckooSet()
{
    the_capacity = INITIAL_CAPACITY;
    the_size = 0;
    the_table[0] = new ProbeSet<TT, Comparator>[the_capacity];
    the_table[1] = new ProbeSet<TT, Comparator>[the_capacity];
}

template <class Pheet, typename TT, class Comparator>
CuckooSet<Pheet, TT, Comparator>::~CuckooSet()
{
    delete[] the_table[0];
    delete[] the_table[1];
}

template <class Pheet, typename TT, class Comparator>
void
CuckooSet<Pheet, TT, Comparator>::put(const TT &item)
{
    LockGuard lock(this, item);

    if (contains_nolock(item)) {
        return;
    }

    const size_t hash0 = h0(item) % the_capacity;
    const size_t hash1 = h1(item) % the_capacity;

    ProbeSet<TT, Comparator> *set0 = the_table[0] + hash0;
    ProbeSet<TT, Comparator> *set1 = the_table[1] + hash1;

    int i = -1;
    size_t h;
    bool must_resize = false;

    if (set0->size() < PROBE_THRESHOLD) {
        set0->add(item);
        the_size++;
        return;
    } else if (set1->size() < PROBE_THRESHOLD) {
        set1->add(item);
        the_size++;
        return;
    } else if (set0->size() < PROBE_SIZE) {
        set0->add(item);
        the_size++;
        i = 0;
        h = hash0;
    } else if (set1->size() < PROBE_SIZE) {
        set1->add(item);
        the_size++;
        i = 1;
        h = hash1;
    } else {
        must_resize = true;
    }

    lock.release();

    if (must_resize) {
        resize();
        put(item);
    } else if (i != -1 && !relocate(i, h)) {
        resize();
    }
}

template <class Pheet, typename TT, class Comparator>
bool
CuckooSet<Pheet, TT, Comparator>::contains(const TT &item)
{
    LockGuard lock(this, item);
    return contains_nolock(item);
}

template <class Pheet, typename TT, class Comparator>
bool
CuckooSet<Pheet, TT, Comparator>::contains_nolock(const TT &item)
{
    const size_t hash0 = h0(item) % the_capacity;
    ProbeSet<TT, Comparator> *set0 = the_table[0] + hash0;

    const size_t hash1 = h1(item) % the_capacity;
    ProbeSet<TT, Comparator> *set1 = the_table[1] + hash1;

    return (set0->contains(item) || set1->contains(item));
}

template <class Pheet, typename TT, class Comparator>
bool
CuckooSet<Pheet, TT, Comparator>::remove(const TT &item)
{
    LockGuard lock(this, item);

    const size_t hash0 = h0(item) % the_capacity;
    ProbeSet<TT, Comparator> *set0 = the_table[0] + hash0;

    if (set0->contains(item)) {
        set0->remove(item);
        the_size--;
        return true;
    }

    const size_t hash1 = h1(item) % the_capacity;
    ProbeSet<TT, Comparator> *set1 = the_table[1] + hash1;

    if (set1->contains(item)) {
        set1->remove(item);
        the_size--;
        return true;
    }

    return false;
}

template <class Pheet, typename TT, class Comparator>
bool
CuckooSet<Pheet, TT, Comparator>::is_empty()
{
    return size() == 0;
}

template <class Pheet, typename TT, class Comparator>
size_t
CuckooSet<Pheet, TT, Comparator>::size()
{
    return the_size.load();
}

template <class Pheet, typename TT, class Comparator>
void
CuckooSet<Pheet, TT, Comparator>::acquire(const TT &item)
{
    the_mutex.lock();
}

template <class Pheet, typename TT, class Comparator>
void
CuckooSet<Pheet, TT, Comparator>::release(const TT &item)
{
    the_mutex.unlock();
}

template <class Pheet, typename TT, class Comparator>
void
CuckooSet<Pheet, TT, Comparator>::resize()
{
    std::lock_guard<std::mutex> lock(the_mutex);

    const size_t prev_capacity = the_capacity;
    the_capacity = prev_capacity * 2;

    ProbeSet<TT, Comparator> *next0 = new ProbeSet<TT, Comparator>[the_capacity];
    ProbeSet<TT, Comparator> *next1 = new ProbeSet<TT, Comparator>[the_capacity];

    for (int i = 0; i < prev_capacity; i++) {
        next0[i] = the_table[0][i];
        next1[i] = the_table[1][i];
    }

    delete[] the_table[0];
    delete[] the_table[1];

    the_table[0] = next0;
    the_table[1] = next1;
}

template <class Pheet, typename TT, class Comparator>
bool
CuckooSet<Pheet, TT, Comparator>::relocate(const int k, const size_t h)
{
    assert(k == 0 || k == 1);

    size_t hi = h, hj;
    int i = k;
    int j = 1 - i;

    for (int round = 0; round < RELOCATE_LIMIT; round++) {
        ProbeSet<TT, Comparator> *set_i = the_table[i] + hi;
        const TT y = set_i->first();
        hj = ((i == 0) ? h1(y) : h0(y)) % the_capacity;

        LockGuard lock(this, y);

        ProbeSet<TT, Comparator> *set_j = the_table[j] + hj;

        if (set_i->contains(y)) {
            set_i->remove(y);
            if (set_j->size() < PROBE_THRESHOLD) {
                set_j->add(y);
                return true;
            } else if (set_j->size() < PROBE_SIZE) {
                set_j->add(y);
                j = i;
                i = 1 - i;
                hi = hj;
            } else {
                set_i->add(y);
                return false;
            }
        } else if (set_i->size() >= PROBE_THRESHOLD) {
           continue;
        } else {
           return true;
        }
    }

    return false;
}

template <class Pheet, typename TT, class Comparator>
void
CuckooSet<Pheet, TT, Comparator>::print_name()
{
    std::cout << "CuckooSet"; 
}

template class CuckooSet<
    pheet::PheetEnv<
        pheet::BStrategyScheduler,
        pheet::SystemModel,
        pheet::Primitives,
        pheet::DataStructures,
        pheet::ConcurrentDataStructures>,
    unsigned long,
    std::less<unsigned long> >;