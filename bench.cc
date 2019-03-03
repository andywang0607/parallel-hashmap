#include <inttypes.h>


#ifdef STL_UNORDERED
    #include <unordered_map>
    #define MAPNAME std::unordered_map
#elif defined(ABSEIL_FLAT)
    #include "absl/container/flat_hash_map.h"
    #define MAPNAME absl::flat_hash_map
#elif defined(ABSEIL_PARALLEL_FLAT)
    #include "absl/container/parallel_flat_hash_map.h"
    #define MAPNAME absl::parallel_flat_hash_map
    #define MT_SUPPORT 1
#endif

#define xstr(s) str(s)
#define str(s) #s

using hash_t     = MAPNAME<int64_t, int64_t>;
using str_hash_t = MAPNAME<const char *, int64_t>;

#include <cassert>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <thread>  
#include <chrono>
#include <ostream>
#include "meminfo.h"
#include <vector>
using std::vector;

int64_t _abs(int64_t x) { return (x < 0) ? -x : x; }


// --------------------------------------------------------------------------
class Timer 
{
    typedef std::chrono::high_resolution_clock high_resolution_clock;
    typedef std::chrono::milliseconds milliseconds;

public:
    explicit Timer(bool run = false) { if (run) reset(); }
    void reset() { _start = high_resolution_clock::now(); }

    milliseconds elapsed() const
    {
        return std::chrono::duration_cast<milliseconds>(high_resolution_clock::now() - _start);
    }

private:
    high_resolution_clock::time_point _start;
};


// --------------------------------------------------------------------------
//  from: https://github.com/preshing/RandomSequence
// --------------------------------------------------------------------------
class RSU
{
private:
    unsigned int m_index;
    unsigned int m_intermediateOffset;

    static unsigned int permuteQPR(unsigned int x)
    {
        static const unsigned int prime = 4294967291u;
        if (x >= prime)
            return x;  // The 5 integers out of range are mapped to themselves.
        unsigned int residue = ((unsigned long long) x * x) % prime;
        return (x <= prime / 2) ? residue : prime - residue;
    }

public:
    RSU(unsigned int seedBase, unsigned int seedOffset)
    {
        m_index = permuteQPR(permuteQPR(seedBase) + 0x682f0161);
        m_intermediateOffset = permuteQPR(permuteQPR(seedOffset) + 0x46790905);
    }

    unsigned int next()
    {
        return permuteQPR((permuteQPR(m_index++) + m_intermediateOffset) ^ 0x5bf03635);
    }
};

// --------------------------------------------------------------------------
char * new_string_from_integer(int num)
{
    int ndigits = num == 0 ? 1 : (int)log10(num) + 1;
    char * str = (char *)malloc(ndigits + 1);
    sprintf(str, "%d", num);
    return str;
}

// --------------------------------------------------------------------------
template <class T> 
void _fill(vector<T> &v)
{
    srand(1);   // for a fair/deterministic comparison 
    for (size_t i = 0, sz = v.size(); i < sz; ++i) 
        v[i] = (T)(i * 10 + rand() % 10);
}

// --------------------------------------------------------------------------
template <class T> 
void _shuffle(vector<T> &v)
{
    for (size_t n = v.size(); n >= 2; --n)
        std::swap(v[n - 1], v[static_cast<unsigned>(rand()) % n]);
}

// --------------------------------------------------------------------------
template <class T, class HT>
Timer _fill_random(vector<T> &v, HT &hash)
{
    _fill<T>(v);
    _shuffle<T>(v);
    
    Timer timer(true);

    for (size_t i = 0, sz = v.size(); i < sz; ++i)
        hash.insert(typename HT::value_type(v[i], 0));
    return timer;
}

// --------------------------------------------------------------------------
void out(const char* test, int64_t cnt, const char* map, const Timer &t)
{
    printf("%s,time,%lld,%s,%f\n", test, cnt, map, (float)((double)t.elapsed().count() / 1000));
}

// --------------------------------------------------------------------------
void outmem(const char* test, int64_t cnt, const char* map, uint64_t mem)
{
    printf("%s,memory,%lld,%s,%lld\n", test, cnt, map, mem);
}

static bool all_done = false;
static int64_t num_keys[16] = { 0 };
static int64_t loop_idx = 0;
static int64_t inner_cnt = 0;
static const char *test = "random";

// --------------------------------------------------------------------------
template <class HT>
void _fill_random_inner(int64_t cnt, HT &hash, RSU &rsu)
{
    for (int64_t i=0; i<cnt; ++i)
    {
        hash.insert(typename HT::value_type(rsu.next(), 0));
        ++num_keys[0];
    }
}

// --------------------------------------------------------------------------
template <class HT>
struct TD
{
    int64_t thread_idx;
    int64_t num_threads;
    int64_t cnt;
    HT     &hash;
    RSU     rsu;   // generates a random sequence of unique integers
};

// --------------------------------------------------------------------------
template <class HT>
void _fill_random_inner_thr(TD<HT> td)
{
#ifdef MT_SUPPORT
    typename HT::hasher hasher;                         // get hasher object from the hash table
    size_t modulo = td.hash.subcnt() / td.num_threads;  // subcnt() returns the number of subtables

    for (int64_t i=0; i<td.cnt; ++i)                    // iterate over all values
    {
        unsigned int key = td.rsu.next();               // get next key to insert
        size_t hash = hasher(key);                      // compute its hash
        size_t idx  = td.hash.subidx(hash);             // compute the subtable index for this hash
        if (idx / modulo == td.thread_idx)              // if the subtable is suitable for this thread
        {
            td.hash.insert(typename HT::value_type(key, 0));  // insert the value
            ++(num_keys[td.thread_idx]);                      // increment count of inserted values
        }
    }
#endif
}

// --------------------------------------------------------------------------
template <class HT>
void _fill_random_inner_mt(int64_t cnt, HT &hash, RSU &rsu)
{
    constexpr int64_t num_threads = 8;   // has to be a power of two
    std::unique_ptr<std::thread> threads[num_threads];

    for (int64_t i=0; i<num_threads; ++i)
    {
        TD<HT> td {i, num_threads, cnt, hash, rsu};
        threads[i].reset(new std::thread(_fill_random_inner_thr<HT>, td));
    }

    // rsu passed by value to threads... we need to increment the reference object
    for (int64_t i=0; i<cnt; ++i)
        rsu.next();
    
    for (int64_t i=0; i<num_threads; ++i)
        threads[i]->join();
}

// --------------------------------------------------------------------------
size_t total_num_keys()
{
    size_t n = 0;
    for (int i=0; i<16; ++i)
        n += num_keys[i];
    return n;
}
    
// --------------------------------------------------------------------------
template <class HT>
Timer _fill_random2(int64_t cnt, HT &hash)
{
    test = "random";
    unsigned int seed = 76687;
	RSU rsu(seed, seed + 1);

    Timer timer(true);
    const int64_t num_loops = 10;
    inner_cnt = cnt / num_loops;

    for (int i=0; i<16; ++i)
        num_keys[i] = 0;

    for (loop_idx=0; loop_idx<num_loops; ++loop_idx)
    {
        if (!strcmp(xstr(MAPNAME),"absl::parallel_flat_hash_map"))
        {
            // multithreaded insert
            _fill_random_inner_mt(inner_cnt, hash, rsu);
        }
        else
            _fill_random_inner(inner_cnt, hash, rsu);
        
        out(test, total_num_keys(), xstr(MAPNAME), timer);
    }
    fprintf(stderr, "inserted %.2lfM\n", (double)hash.size() / 1000000);
    return timer;
}

// --------------------------------------------------------------------------
template <class T, class HT>
Timer _lookup(vector<T> &v, HT &hash, size_t &num_present)
{
    _fill_random(v, hash);

    num_present = 0;
    size_t max_val = v.size() * 10;
    Timer timer(true);

    for (size_t i = 0, sz = v.size(); i < sz; ++i)
    {
        num_present += (size_t)(hash.find(v[i]) != hash.end());
        num_present += (size_t)(hash.find((T)(rand() % max_val)) != hash.end());
    }
    return timer;
}

// --------------------------------------------------------------------------
template <class T, class HT>
Timer _delete(vector<T> &v, HT &hash)
{
    _fill_random(v, hash);
    _shuffle(v); // don't delete in insertion order

    Timer timer(true);

    for(size_t i = 0, sz = v.size(); i < sz; ++i)
        hash.erase(v[i]);
    return timer;
}

// --------------------------------------------------------------------------
void memlog()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint64_t nbytes_old_out = spp::GetProcessMemoryUsed();
    uint64_t nbytes_old     = spp::GetProcessMemoryUsed(); // last non outputted mem measurement
    outmem(test, 0, xstr(MAPNAME), nbytes_old);
    int64_t last_loop = 0;

    while (!all_done)
    {
        uint64_t nbytes = spp::GetProcessMemoryUsed();

        if ((double)_abs(nbytes - nbytes_old_out) / nbytes_old_out > 0.03 ||
            (double)_abs(nbytes - nbytes_old) / nbytes_old > 0.01)
        {
            if ((double)(nbytes - nbytes_old) / nbytes_old > 0.03)
                outmem(test, total_num_keys() - 1, xstr(MAPNAME), nbytes_old);
            outmem(test, total_num_keys(), xstr(MAPNAME), nbytes);
            nbytes_old_out = nbytes;
            last_loop = loop_idx;
        } 
        else if (loop_idx > last_loop)
        {
            outmem(test, total_num_keys(), xstr(MAPNAME), nbytes);
            nbytes_old_out = nbytes;
            last_loop = loop_idx;
        }
        nbytes_old = nbytes;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}


// --------------------------------------------------------------------------
int main(int argc, char ** argv)
{
    int64_t num_keys = atoi(argv[1]);
    int64_t i, value = 0;

    if(argc <= 2)
        return 1;

    hash_t     hash; 
    str_hash_t str_hash;

    srand(1); // for a fair/deterministic comparison 
    Timer timer(true);

    std::thread t1(memlog);

    try 
    {
        if(!strcmp(argv[2], "sequential"))
        {
            for(i = 0; i < num_keys; i++)
                hash.insert(hash_t::value_type(i, value));
        }
#if 0
        else if(!strcmp(argv[2], "random"))
        {
            vector<int64_t> v(num_keys);
            timer = _fill_random(v, hash);
            out("random", num_keys, xstr(MAPNAME), timer);
        }
#endif
        else if(!strcmp(argv[2], "random"))
        {
            timer = _fill_random2(num_keys, hash);
            //out("random", num_keys, xstr(MAPNAME), timer);
            //fprintf(stderr, "inserted %llu\n", hash.size());
        }
        else if(!strcmp(argv[2], "lookup"))
        {
            vector<int64_t> v(num_keys);
            size_t num_present;

            timer = _lookup(v, hash, num_present);
            //fprintf(stderr, "found %llu\n", num_present);
        }
        else if(!strcmp(argv[2], "delete"))
        {
            vector<int64_t> v(num_keys);
            timer = _delete(v,  hash);
        }
        else if(!strcmp(argv[2], "sequentialstring"))
        {
            for(i = 0; i < num_keys; i++)
                str_hash.insert(str_hash_t::value_type(new_string_from_integer(i), value));
        }
        else if(!strcmp(argv[2], "randomstring"))
        {
            for(i = 0; i < num_keys; i++)
                str_hash.insert(str_hash_t::value_type(new_string_from_integer((int)rand()), value));
        }
        else if(!strcmp(argv[2], "deletestring"))
        {
            for(i = 0; i < num_keys; i++)
                str_hash.insert(str_hash_t::value_type(new_string_from_integer(i), value));
            timer.reset();
            for(i = 0; i < num_keys; i++)
                str_hash.erase(new_string_from_integer(i));
        }

 
        //printf("%f\n", (float)((double)timer.elapsed().count() / 1000));
        fflush(stdout);
        //std::this_thread::sleep_for(std::chrono::seconds(1000));
    }
    catch (...)
    {
    }

    all_done = true;
    t1.join();
    return 0;
}