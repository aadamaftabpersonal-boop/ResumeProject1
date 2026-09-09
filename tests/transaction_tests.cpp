#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include "ddb/concurrency/lock_manager.h"
namespace { int failures=0;
#define EXPECT(x) do{if(!(x)){++failures;std::cerr<<__FUNCTION__<<": " #x "\n";}}while(false)
using namespace ddb::concurrency;
ResourceId page(std::uint64_t id){return ResourceId(id);}
void shared_and_exclusive(){TransactionManager tm;LockManager lm;auto a=tm.begin(),b=tm.begin();EXPECT(lm.lock_shared(a,page(1)));EXPECT(lm.lock_shared(b,page(1)));EXPECT(lm.held_lock_count(a)==1);lm.commit(a);lm.commit(b);EXPECT(lm.lock_count()==0);auto x=tm.begin(),y=tm.begin();EXPECT(lm.lock_exclusive(x,page(2)));EXPECT(!lm.lock_shared(y,page(2)));EXPECT(y.state()==TransactionState::Aborted);lm.commit(x);}
void blocking_and_upgrade(){TransactionManager tm;LockManager lm;auto waiter=tm.begin(),owner=tm.begin();EXPECT(lm.lock_exclusive(owner,page(3)));std::mutex mutex;std::condition_variable cv;bool started=false,acquired=false;std::thread thread([&]{ {std::lock_guard g(mutex);started=true;}cv.notify_one();const bool ok=lm.lock_shared(waiter,page(3));{std::lock_guard g(mutex);acquired=ok;}cv.notify_one();});{std::unique_lock g(mutex);cv.wait(g,[&]{return started;});EXPECT(!acquired);}lm.commit(owner);{std::unique_lock g(mutex);cv.wait(g,[&]{return acquired;});}thread.join();lm.commit(waiter);auto up=tm.begin();EXPECT(lm.lock_shared(up,page(4)));EXPECT(lm.lock_exclusive(up,page(4)));EXPECT(lm.holds(up,page(4),LockMode::Exclusive));lm.commit(up);}
void upgrade_conflict_and_2pl(){TransactionManager tm;LockManager lm;auto old=tm.begin(),young=tm.begin();EXPECT(lm.lock_shared(old,page(5)));EXPECT(lm.lock_shared(young,page(5)));std::thread waiting([&]{EXPECT(!lm.lock_exclusive(young,page(5)));});waiting.join();EXPECT(young.state()==TransactionState::Aborted);EXPECT(lm.lock_exclusive(old,page(5)));EXPECT(lm.unlock(old,page(5)));EXPECT(old.state()==TransactionState::Shrinking);EXPECT(!lm.lock_shared(old,page(6)));lm.abort(old);EXPECT(lm.lock_count()==0);}
void wait_die_and_stress(){TransactionManager tm;LockManager lm;auto old=tm.begin(),young=tm.begin();EXPECT(lm.lock_exclusive(young,page(7)));std::mutex mutex;std::condition_variable cv;bool started=false,got=false;std::thread wait([&]{{std::lock_guard g(mutex);started=true;}cv.notify_one();const bool result=lm.lock_shared(old,page(7));{std::lock_guard g(mutex);got=result;}cv.notify_one();});{std::unique_lock g(mutex);cv.wait(g,[&]{return started;});EXPECT(!got);}lm.commit(young);{std::unique_lock g(mutex);cv.wait(g,[&]{return got;});}wait.join();lm.commit(old);std::vector<std::thread> workers;for(int n=0;n<4;++n)workers.emplace_back([&]{for(int i=0;i<100;++i){auto tx=tm.begin();const auto r=page(static_cast<std::uint64_t>(i%3));if(lm.lock_shared(tx,r))lm.commit(tx);else EXPECT(tx.state()==TransactionState::Aborted);}});for(auto& worker:workers)worker.join();EXPECT(lm.lock_count()==0);}
}
int main(){shared_and_exclusive();blocking_and_upgrade();upgrade_conflict_and_2pl();wait_die_and_stress();if(failures){std::cerr<<failures<<" failures\n";return 1;}std::cout<<"All transaction tests passed\n";}
