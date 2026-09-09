#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include "ddb/concurrency/lock_manager.h"
using namespace ddb::concurrency;
namespace { constexpr int kOperations=100000; void require(bool value){if(!value)throw std::runtime_error("lock benchmark operation failed");} }
int main(){auto measure=[](const char* label,auto&& operation){auto start=std::chrono::steady_clock::now();operation();auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::cout<<label<<": "<<seconds<<" s, "<<kOperations/seconds<<" ops/s\n";};TransactionManager tm;LockManager lm;measure("Uncontended S lock/unlock",[&]{for(int i=0;i<kOperations;++i){auto tx=tm.begin();require(lm.lock_shared(tx,ResourceId(1)));require(lm.unlock(tx,ResourceId(1)));lm.commit(tx);}});measure("Uncontended X lock/unlock",[&]{for(int i=0;i<kOperations;++i){auto tx=tm.begin();require(lm.lock_exclusive(tx,ResourceId(1)));require(lm.unlock(tx,ResourceId(1)));lm.commit(tx);}});measure("Concurrent shared locking",[&]{std::vector<std::thread> ts;for(int j=0;j<4;++j)ts.emplace_back([&]{for(int i=0;i<kOperations/4;++i){auto tx=tm.begin();require(lm.lock_shared(tx,ResourceId(2)));lm.commit(tx);}});for(auto&t:ts)t.join();});measure("Contended exclusive locking",[&]{std::vector<std::thread> ts;for(int j=0;j<4;++j)ts.emplace_back([&]{for(int i=0;i<kOperations/4;++i){auto tx=tm.begin();if(lm.lock_exclusive(tx,ResourceId(3)))lm.commit(tx);else if(tx.state()!=TransactionState::Aborted)throw std::runtime_error("unexpected exclusive lock failure");}});for(auto&t:ts)t.join();});}
