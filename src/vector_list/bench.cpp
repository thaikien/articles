#include <random>
#include <array>
#include <vector>
#include <list>
#include <algorithm>
#include <deque>
#include <thread>
#include <iostream>
#include <chrono>
#include <cstdint>

#include "graphs.hpp"

static const std::size_t REPEAT = 2;

namespace {
  
  using std::chrono::milliseconds;
  using std::chrono::microseconds;
  using Clock = std::chrono::high_resolution_clock;

  struct NonMovable {
    NonMovable()                                = default;
    NonMovable(const NonMovable &)              = default;
    NonMovable &operator=(const NonMovable &)   = default;
    NonMovable(NonMovable &&)                   = delete;
    NonMovable &operator=(NonMovable &&)        = delete;
  };
  
  struct Movable {
    Movable()                                   = default;
    Movable(const Movable &)                    = default;
    Movable &operator=(const Movable &)         = default;
    Movable(Movable &&)                         = default;
    Movable &operator=(Movable &&)              = default;
  };
  
  struct MovableNoExcept {
    MovableNoExcept()                                   = default;
    MovableNoExcept(const MovableNoExcept &)            = default;
    MovableNoExcept &operator=(const MovableNoExcept &) = default;
    MovableNoExcept(MovableNoExcept &&) noexcept        = default;
    MovableNoExcept &operator=(MovableNoExcept &&) noexcept = default;
  };
  
  template<size_t N>
  struct Trivial {
    std::size_t a;
    std::array<unsigned char, N-sizeof(a)> b;
    bool operator<(const Trivial &other) const { return a < other.a; }
  };

  template<>
  struct Trivial<sizeof(std::size_t)> {
    std::size_t a;
    bool operator<(const Trivial &other) const { return a < other.a; }
  };

  // non trivial, quite expensive to copy but easy to move
  template<class Base = MovableNoExcept>
  class NonTrivial1 : Base {
    std::string data{"some pretty long string to make sure it is not optimized with SSO"};
  public: 
    std::size_t a{0};
    NonTrivial1() = default;
    NonTrivial1(std::size_t a): a(a) {}
    ~NonTrivial1() = default;
    bool operator<(const NonTrivial1 &other) const { return a < other.a; }
  };
  
  // non trivial, quite expensive to copy and move
  template<size_t N, class Base = MovableNoExcept>
  class NonTrivial2 : Base {
  public:
    std::size_t a = 0;
  private:
    std::array<unsigned char, N-sizeof(a)> b;
  public:
    NonTrivial2() = default;
    NonTrivial2(std::size_t a): a(a) {}
    ~NonTrivial2() = default;
    bool operator<(const NonTrivial2 &other) const { return a < other.a; }
  };
  
  // types to benchmark
  using Small   = Trivial<8>;         static_assert(sizeof(Small)   == 8,      "Invalid size");
  using Medium  = Trivial<32>;        static_assert(sizeof(Medium)  == 32,     "Invalid size");
  using Large   = Trivial<128>;       static_assert(sizeof(Large)   == 128,    "Invalid size");
  using Huge    = Trivial<1024>;      static_assert(sizeof(Huge)    == 1024,   "Invalid size");
  using Monster = Trivial<4*1024>;    static_assert(sizeof(Monster) == 4*1024, "Invalid size");
  using NonTrivial = NonTrivial1<>;
  
  // invariants check
  static_assert(std::is_trivial<Small>::value,   "Expected trivial type");
  static_assert(std::is_trivial<Medium>::value,  "Expected trivial type");
  static_assert(std::is_trivial<Large>::value,   "Expected trivial type");
  static_assert(std::is_trivial<Huge>::value,    "Expected trivial type");
  static_assert(std::is_trivial<Monster>::value, "Expected trivial type");
  
  static_assert(!std::is_trivial<NonTrivial>::value, "Expected non trivial type");
  
  
  // create policies
  template<class Container>
  struct Empty {
    inline static Container make(std::size_t) { return Container(); }
  };
   
  template<class Container>
  struct Filled {
    inline static Container make(std::size_t size) { return Container(size); }
  };
  
 template<class Container>
  struct FilledRandom {
    static std::vector<size_t> v;
    inline static Container make(std::size_t size)
    {
      // prepare randomized data that will have all the integers from the range
      if(v.size() != size) {
        v.clear();
        v.resize(size);
        std::iota(begin(v), end(v), 0);
        std::shuffle(begin(v), end(v), std::mt19937());
      }
      
      // fill with randomized data
      Container c;
      for(auto val : v)
        c.push_back({val});
      return c;
    }
  };
  template<class Container> std::vector<size_t> FilledRandom<Container>::v;
  
  template<class Container>
  struct SmartFilled {
    inline static std::unique_ptr<Container> make(std::size_t size)
    { return std::unique_ptr<Container>(new Container(size)); }
  };
  
  
  // testing policies
  template<class Container>
  struct NoOp {
    inline static void run(Container &, std::size_t) {}
  };
  
  template<class Container>
  struct ReserveSize {
    inline static void run(Container &c, std::size_t size) { c.reserve(size); }
  };
  
  template<class Container>
  struct FillBack {
    static const typename Container::value_type value;
    inline static void run(Container &c, std::size_t size)
    { std::fill_n(std::back_inserter(c), size, value); }
  };
  template<class Container> const typename Container::value_type FillBack<Container>::value{};
  
  template<class Container>
  struct FillFront {
    static const typename Container::value_type value;
    inline static void run(Container &c, std::size_t size)
    { std::fill_n(std::front_inserter(c), size, value); }
  };
  template<class Container> const typename Container::value_type FillFront<Container>::value{};
  
  template<class T>
  struct FillFront<std::vector<T> > {
    static const T value;
    inline static void run(std::vector<T> &c, std::size_t size)
    {
      for(std::size_t i=0; i<size; ++i)
        c.insert(begin(c), value);
    }
  };
  template<class T> const T FillFront<std::vector<T> >::value{};
  
  template<class Container>
  struct Find {
    inline static void run(Container &c, std::size_t size)
    {
      for(std::size_t i=0; i<size; ++i) {
        // hand written comparison to eliminate temporary object creation
        std::find_if(begin(c), end(c), [&](decltype(*begin(c)) v){ return v.a == i; });
      }
    }
  };
  
  template<class Container>
  struct Insert {
    inline static void run(Container &c, std::size_t size)
    {
      for(std::size_t i=0; i<1000; ++i) {
        // hand written comparison to eliminate temporary object creation
        auto it = std::find_if(begin(c), end(c), [&](decltype(*begin(c)) v){ return v.a == i; });
        c.insert(it, {size + i});
      }
    }
  };
  
  template<class Container>
  struct Remove {
    inline static void run(Container &c, std::size_t size)
    {
      for(std::size_t i=0; i<1000; ++i) {
        // hand written comparison to eliminate temporary object creation
        auto it = std::find_if(begin(c), end(c), [&](decltype(*begin(c)) v){ return v.a == i; });
        c.erase(it);
      }
    }
  };

  template<class Container>
  struct Sort {
    inline static void run(Container &c, std::size_t size)
    {
      std::sort(c.begin(), c.end());
    }
  };
  
  template<class T>
  struct Sort<std::list<T> > {
    inline static void run(std::list<T> &c, std::size_t size)
    {
      c.sort();
    }
  };

  template<class Container>
  struct SmartDelete {
    inline static void run(Container &c, std::size_t size) { c.reset(); }
  };

  template<class Container>
  struct RandomSortedInsert {
    static std::mt19937 generator;
    static std::uniform_int_distribution<std::size_t> distribution;
    inline static void run(Container &c, std::size_t size)
    {
      for(std::size_t i=0; i<size; ++i){
        auto val = distribution(generator);
        // hand written comparison to eliminate temporary object creation
        c.insert(std::find_if(begin(c), end(c), [&](decltype(*begin(c)) v){ return v.a >= val; }), {val});
      }
    }
  };
  template<class Container> std::mt19937 RandomSortedInsert<Container>::generator;
  template<class Container> std::uniform_int_distribution<std::size_t> RandomSortedInsert<Container>::distribution(0, std::numeric_limits<std::size_t>::max() - 1);
  
  
  // variadic policy runner
  template<class Container>
  inline static void run(Container &, std::size_t)
  {
  }
  
  template<template<class> class Test, template<class> class ...Rest, class Container>
  inline static void run(Container &container, std::size_t size)
  {
    Test<Container>::run(container, size);
    run<Rest...>(container, size);
  }
  
  
  // test
  template<typename Container,
           typename DurationUnit,
           template<class> class CreatePolicy,
           template<class> class ...TestPolicy>
  void bench(const std::string& type, const std::initializer_list<int> &sizes)
  {
    // create an element to copy so the temporary creation
    // and initialization will not be accounted in a benchmark
    typename Container::value_type value;
    for(auto size : sizes) {
      Clock::duration duration;
      
      for(std::size_t i=0; i<REPEAT; ++i) {
        auto container = CreatePolicy<Container>::make(size);
        
        Clock::time_point t0 = Clock::now();
        
        // run test
        run<TestPolicy...>(container, size);
        
        Clock::time_point t1 = Clock::now();
        duration += t1 - t0;
      }
      
      graphs::new_result(type, std::to_string(size),
                         std::chrono::duration_cast<DurationUnit>(duration).count() / REPEAT);
    }
  }
  
  
  // Launch benchmarks on different sizes
  template<typename T>
  void bench()
  {
    std::string size_str = std::to_string(sizeof(T));
    
    {
      graphs::new_graph("fill_back_" + size_str, "fill_back - "  + size_str + " byte", "us");
      auto sizes = { 100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000, 1000000 };
      bench<std::vector<T>, microseconds, Empty, ReserveSize, FillBack>("vector_pre", sizes);
      bench<std::vector<T>, microseconds, Empty, FillBack>("vector", sizes);
      bench<std::list<T>,   microseconds, Empty, FillBack>("list",   sizes);
      bench<std::deque<T>,  microseconds, Empty, FillBack>("deque",  sizes);
    }
  
    //Result are clear enough with very small size
    if(sizeof(T) == sizeof(Small)) {
      graphs::new_graph("fill_front_" + size_str, "fill_front - "  + size_str + " byte", "ms");
      auto sizes = { 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };
      bench<std::vector<T>, milliseconds, Empty, FillFront>("vector", sizes);
      bench<std::list<T>,   milliseconds, Empty, FillFront>("list",   sizes);
      bench<std::deque<T>,  milliseconds, Empty, FillFront>("deque",  sizes);
    }
  
    {
      graphs::new_graph("linear_search_" + size_str, "linear_search - "  + size_str + " byte", "us");
      auto sizes = {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000};
      bench<std::vector<T>, microseconds, FilledRandom, Find>("vector", sizes);
      bench<std::list<T>,   microseconds, FilledRandom, Find>("list",   sizes);
      bench<std::deque<T>,  microseconds, FilledRandom, Find>("deque",  sizes);
    }

    {
      graphs::new_graph("random_insert_" + size_str, "random_insert - "  + size_str + " byte", "ms");
      auto sizes = {10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};
      bench<std::vector<T>, milliseconds, FilledRandom, Insert>("vector", sizes);
      bench<std::list<T>,   milliseconds, FilledRandom, Insert>("list",   sizes);
      bench<std::deque<T>,  milliseconds, FilledRandom, Insert>("deque",  sizes);
    }
  
    {
      graphs::new_graph("random_remove_" + size_str, "random_remove - "  + size_str + " byte", "ms");
      auto sizes = {10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};
      bench<std::vector<T>, milliseconds, FilledRandom, Remove>("vector", sizes);
      bench<std::list<T>,   milliseconds, FilledRandom, Remove>("list",   sizes);
      bench<std::deque<T>,  milliseconds, FilledRandom, Remove>("deque",  sizes);
    }
  
    {
      graphs::new_graph("sort_" + size_str, "sort - "  + size_str + " byte", "ms");
      auto sizes = {100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000, 1000000};
      bench<std::vector<T>, milliseconds, FilledRandom, Sort>("vector", sizes);
      bench<std::list<T>,   milliseconds, FilledRandom, Sort>("list",   sizes);
      bench<std::deque<T>,  milliseconds, FilledRandom, Sort>("deque",  sizes);
    }
  
    {
      graphs::new_graph("destruction_" + size_str, "destruction - "  + size_str + " byte", "us");
      auto sizes = {100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000, 1000000};
      bench<std::vector<T>, microseconds, SmartFilled, SmartDelete>("vector", sizes);
      bench<std::list<T>,   microseconds, SmartFilled, SmartDelete>("list",   sizes);
      bench<std::deque<T>,  microseconds, SmartFilled, SmartDelete>("deque",  sizes);
    }

    //Result are clear enough with very small size
    if(sizeof(T) == sizeof(Small)) {
      graphs::new_graph("number_crunching", "number_crunching", "ms");
      auto sizes = {10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};
      bench<std::vector<T>, milliseconds, Empty, RandomSortedInsert>("vector", sizes);
      bench<std::list<T>,   milliseconds, Empty, RandomSortedInsert>("list",   sizes);
      bench<std::deque<T>,  milliseconds, Empty, RandomSortedInsert>("deque",  sizes);
    }
  }

} //end of anonymous namespace


int main()
{
  bench<Small>();
  bench<Medium>();
  bench<Large>();
  bench<Huge>();
  bench<Monster>();
  bench<NonTrivial>();
  graphs::output(graphs::Output::GOOGLE);
}
