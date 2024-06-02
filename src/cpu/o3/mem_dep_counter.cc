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

  //Assumes there is only one thread from the head of which ROB inserts/pops
  //insts
  assert (inst->thread->getTC()->threadId() == 0);
};

void MemDepCounter::remove_squashed(const o3::DynInstPtr &inst){

      //Stats
    if (inst->isLoad()){
      cpu->cpuStats.smLoads++;
      cpu->cpuStats.smSquashedLoads++;
    } else if (inst->isStore() || inst->isAtomic()){
      cpu->cpuStats.smStores++;
      cpu->cpuStats.smSquashedStores++;
    }

    cpu->cpuStats.smUops++;
    cpu->cpuStats.smSquashedUops++;

    inst->effSeqNum = cpu->effGlobalSeqNum;

  //Filter anything that aren't memory operations
  if (!(inst->isLoad() || inst->isStore() || inst->isAtomic())){
    return;
  }

  //Update mem dep violation counting state machine
  if ((bool)inst->mem_violator){
      sm_state = SmState::Possible;
      sm_pc = inst->pcState().instAddr();
      sm_n_visited = inst->n_visited;
      sm_address = inst->effAddr;
      sm_seqnum = inst->seqNum;
      sm_dep = inst->predictedDep;

      sm_dep_pc = inst->mem_violator->pcState().instAddr();
      sm_dep_n_visited = inst->mem_violator->n_visited;
      sm_dep_n_visited_at_detection = inst->mem_violator_n_at_detection;
      sm_dep_address = inst->mem_violator->effAddr;
      sm_n_visited_at_detection = inst->n_visited_at_detection;
      sm_n_visited_at_prediction = inst->n_visited_at_prediction;
  }

  assert(inst->seqNum == in_flight.front()->seqNum);

    uint64_t inst_n_visited =  inst->n_visited;
    Addr pc = inst->pcState().instAddr();

  for (auto it = in_flight.begin(); it != in_flight.end(); it++){
    if ((*it)->pcState().instAddr() == pc &&
      (*it)->n_visited > inst_n_visited){
        DPRINTF(FYPDebug,"MemCounter decrement: PC: %llu, Visited %llu,"
        "seqnum %llu, effadr %llx \n",
        (*it)->pcState().instAddr() , (*it)->n_visited,
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

    //Stats
    if (inst->isLoad()){
      cpu->cpuStats.smLoads++;
    } else if (inst->isStore() || inst->isAtomic()){
      cpu->cpuStats.smStores++;
    }

    cpu->cpuStats.smUops++;

    inst->effSeqNum = cpu->effGlobalSeqNum++;

    //Print heartbeat
    static uint64_t next_heartbeat = 0;
    if (inst->effSeqNum > next_heartbeat){
      printf("Heartbeat: %llu \n", inst->effSeqNum);
      next_heartbeat += 1000000;
    }

    //Memviolation state machine
    if (sm_state == SmState::Possible){
        if (inst->isLoad() || inst->isStore() || inst->isAtomic()){
          if (sm_pc == inst->pcState().instAddr() &&
              sm_n_visited == inst->n_visited &&
              sm_address == inst->effAddr){
                cpu->cpuStats.smMemOrderViolations++;
                inst->sm_violator = true;
                TraceUID tuid = TraceUID( inst->pcState().instAddr(),
                inst->n_visited);

                DPRINTF(FYPDebug, "MemCounter SM violation: [sn:%llu] "
                "[%lli:%lli] [%s] at address %llx \n", inst->seqNum,
                inst->pcState().instAddr(),
                inst->n_visited, inst->isLoad() ? "load" : "not load",
                inst->effAddr );

                if (sm_n_visited != sm_n_visited_at_prediction ||
                  sm_dep_n_visited != sm_dep_n_visited_at_detection){
                  DPRINTF(FYPDebug, "SM violation signature change:"
                " Inst: [%lli:%lli] -> [%lli]; Dep: [%lli:%lli] -> [%lli]\n",
                sm_pc, sm_n_visited, sm_n_visited_at_prediction,
                sm_dep_pc, sm_dep_n_visited, sm_dep_n_visited_at_detection);
                }

                  //Stats
                if (inst->isLoad()){
                  cpu->cpuStats.smTriggeringLoads++;
                } else if (inst->isStore() || inst->isAtomic()){
                  cpu->cpuStats.smTriggeringStores++;
                }

                if (sm_dep){
                  cpu->cpuStats.smMDPMispredictionsFalse++;
                } else{
                  cpu->cpuStats.smMDPMispredictionsCold++;
                }

                cpu->cpuStats.smSquashedMemDepUops
                  .sample(inst->seqNum-sm_seqnum);
              }
        }
        sm_state = SmState::Idle;
    }

    //Filter anything that aren't memory operations from this point onwards
    if (!(inst->isLoad() || inst->isStore() || inst->isAtomic())){
      return;
    }

    assert(inst->seqNum == in_flight.front()->seqNum);

    //Remove committed inst
    in_flight.pop_front();

    //MDP stats
    if (inst->isStore() || inst->isAtomic()){
    inst_history.insert(std::pair<uint64_t, Addr>(inst->seqNum,
      inst->effAddr >> 4));
    }

    if (inst->isLoad() && !inst->sm_violator){
      if (inst->predictedDep != 0){
        if (inst_history.count(inst->predictedDep)){
          if (inst_history[inst->predictedDep] == inst->effAddr >> 4){
            cpu->cpuStats.smMDPOKPred++;
          } else{
            cpu->cpuStats.smMDPOKBadPred++;
          }
        } else {
          cpu->cpuStats.smMDPOKBadPred++;
        }
      }else{
        cpu->cpuStats.smMDPOKNoPred++;
      }
    }

    if (inst_history.size()> 10000){
      inst_history.erase(prev(inst_history.end()));
    }
};

void MemDepCounter::dump_in_flight(){
  for (auto it = in_flight.begin(); it != in_flight.end(); it++){

    DPRINTF(FYPDebug,"MemTracer in flight: %llu:%llu, seqnum %llu, "
    "effadr %llx \n",
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
