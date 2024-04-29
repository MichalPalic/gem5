
#ifndef __CPU_MEM_DEP_COUNTER_HH__
#define __CPU_MEM_DEP_COUNTER_HH__

#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "base/trace.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/o3/dyn_inst_ptr.hh"
#include "params/BaseO3CPU.hh"
#include "sim/sim_exit.hh"

enum class SmState { Idle, Possible};

namespace gem5
{

  class MemDepCounter
  {
    public:

    //State machine vars
    SmState sm_state = SmState::Idle;
    Addr sm_pc = 0;
    uint64_t sm_n_visited = 0;
    Addr sm_address = 0;

    o3::CPU* cpu;

    std::unordered_map<Addr, uint64_t> n_visited;

    std::list<o3::DynInstPtr> in_flight;


    MemDepCounter(o3::CPU * _cpu, const BaseO3CPUParams &params);


    void insert_from_rob(const o3::DynInstPtr &inst);
    void remove_squashed(const o3::DynInstPtr &inst);
    void remove_comitted(const o3::DynInstPtr &inst);
    void dump_in_flight();
    void dump_rob();

  };

}

//Common helper classes
//Setup to allow TraceUID to be used as hashmap key
struct TraceUID
{
  gem5::Addr pc;
  uint64_t n_visited;

  TraceUID(){}
  TraceUID(gem5::Addr pc, uint64_t n_visited): pc(pc), n_visited(n_visited){}

  bool operator==(const TraceUID &other) const
    {
      return (pc == other.pc && n_visited == other.n_visited);
    }
};

  template<>
  struct std::hash<TraceUID>
  {
    std::size_t operator()(const TraceUID& t) const noexcept
    {
      std::size_t h1 = std::hash<uint64_t>{}(t.pc);
      std::size_t h2 = std::hash<uint64_t>{}(t.n_visited);
      return h1 ^ (h2 << 1);
    }
  };

  namespace std {
    string to_string(const TraceUID &t);
  }

  #endif // __CPU_MEM_DEP_COUNTER_HH__
