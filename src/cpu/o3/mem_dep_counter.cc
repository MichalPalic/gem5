/*
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "cpu/o3/mem_dep_counter.hh"

#include "cpu/o3/cpu.hh"
#include "cpu/o3/dyn_inst.hh"

namespace gem5 {
MemDepCounter::MemDepCounter(o3::CPU *_cpu, const BaseO3CPUParams &params)
      : cpu(_cpu){};

void MemDepCounter::insert_from_rob(const o3::DynInstPtr &inst){

  //Filter anything that aren't memory operations
  if (!(inst->isLoad() || inst->isStore() || inst->isAtomic())){
    return;
  }

  auto pc = inst->pcState().instAddr();

  //Initialize/increment visited counter
  if (n_visited.count(pc)){
    n_visited[pc]++;
  }
  else{
    n_visited[pc] = 1;
  }

  //Register instruction to be tracked
  assert(in_flight.size() == 0 || inst->seqNum > in_flight.back()->seqNum);
  in_flight.push_back(inst);

  //Update inst signature
  inst->n_visited = n_visited[pc];

  uint64_t tid = inst->thread->getTC()->threadId();

  //Assumes there is only one thread from the head of which ROB inserts/pops
  //insts
  assert (tid == 0);
};

void MemDepCounter::remove_squashed(const o3::DynInstPtr &inst){

  //Filter anything that aren't memory operations
  if (!(inst->isLoad() || inst->isStore() || inst->isAtomic())){
    return;
  }

  //Update mem dep violation counting state machine
  if (inst->mem_violator){
      sm_state = SmState::Possible;
      sm_pc = inst->pcState().instAddr();
      sm_n_visited = inst->n_visited;
      sm_address = inst->effAddr;
  }

  assert(inst->seqNum == in_flight.front()->seqNum);

    uint64_t inst_n_visited =  inst->n_visited;
    Addr pc = inst->pcState().instAddr();

  //Decrement instruction signature for all following instructions
  for (auto it = in_flight.begin(); it != in_flight.end(); it++){
    if ((*it)->pcState().instAddr() == pc &&
      (*it)->n_visited > inst_n_visited){
        DPRINTF(FYPDebug,"MemCounter decrement: PC: %u, Visited %u, seqnum %u,\
        effadr %u \n", (*it)->pcState().instAddr() , (*it)->n_visited,
        (*it)->seqNum, (*it)->effAddr);
      (*it)->n_visited--;
    }

  }
  //Remove squashed inst
  in_flight.pop_front();

  //Roll back next n_visited to be allocated
  n_visited[pc]--;
};

void MemDepCounter::remove_comitted(const o3::DynInstPtr &inst){

    //Filter anything that aren't memory operations
    if (!(inst->isLoad() || inst->isStore() || inst->isAtomic())){
      return;
    }

    //Memviolation state machine
    if (sm_state == SmState::Possible){
        if (sm_pc == inst->pcState().instAddr() &&
            sm_n_visited == inst->n_visited &&
            sm_address == inst->effAddr){
            cpu->cpuStats.smMemOrderViolations++;
        }
        sm_state = SmState::Idle;
    }

    assert(inst->seqNum == in_flight.front()->seqNum);

    //Remove committed inst
    in_flight.pop_front();

};

void MemDepCounter::dump_in_flight(){
  for (auto it = in_flight.begin(); it != in_flight.end(); it++){

    DPRINTF(FYPDebug,"MemTracer in flight: %u:%u, seqnum %u, effadr %u \n",
      (*it)->pcState().instAddr(), (*it)->n_visited, (*it)->seqNum,
      (*it)->effAddr);
  }
}

void MemDepCounter::dump_rob(){
  for (auto it = cpu->rob.instList[0].begin();
    it != cpu->rob.instList[0].end(); it++){

    DPRINTF(FYPDebug,"MemTracer ROB: %u:%u, seqnum %u, effadr %u \n",
      (*it)->pcState().instAddr(), (*it)->n_visited, (*it)->seqNum,
      (*it)->effAddr);
  }
}

} // namespace gem5


namespace std {
    string to_string(const TraceUID &t){
        return to_string(t.pc) + ':' + to_string(t.n_visited);
    }
}
