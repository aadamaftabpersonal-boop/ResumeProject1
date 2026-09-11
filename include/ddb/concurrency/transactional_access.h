#pragma once
#include <optional>
#include <mutex>
#include <stdexcept>
#include "ddb/buffer/buffer_pool_manager.h"
#include "ddb/concurrency/lock_manager.h"
#include "ddb/execution/table_heap.h"
#include "ddb/index/bplus_tree.h"

namespace ddb::concurrency {
class LockedPage final {
 public:
  LockedPage(ddb::buffer::BufferPoolManager& pool, std::mutex& pool_mutex, ddb::storage::PageId id, ddb::storage::Page* page, bool dirty)
      : pool_(&pool), pool_mutex_(&pool_mutex), id_(id), page_(page), dirty_(dirty) {}
  ~LockedPage() { if(page_){std::lock_guard guard(*pool_mutex_);if(!pool_->unpin_page(id_,dirty_)) std::terminate();} }
  LockedPage(const LockedPage&)=delete; LockedPage& operator=(const LockedPage&)=delete;
  LockedPage(LockedPage&& other) noexcept : pool_(other.pool_),pool_mutex_(other.pool_mutex_),id_(other.id_),page_(other.page_),dirty_(other.dirty_){other.page_=nullptr;}
  [[nodiscard]] ddb::storage::Page& page() const { return *page_; }
 private: ddb::buffer::BufferPoolManager* pool_; std::mutex* pool_mutex_; ddb::storage::PageId id_; ddb::storage::Page* page_; bool dirty_;
};
class TransactionalBufferPool final {
 public:
  TransactionalBufferPool(ddb::buffer::BufferPoolManager& pool,LockManager& locks):pool_(pool),locks_(locks){}
  [[nodiscard]] std::optional<LockedPage> read(Transaction& tx,ddb::storage::PageId id){if(!locks_.lock_shared(tx,ResourceId(id)))return{};std::lock_guard guard(pool_mutex_);auto* p=pool_.fetch_page(id);if(!p)throw std::runtime_error("cannot pin read page");return LockedPage(pool_,pool_mutex_,id,p,false);}
  [[nodiscard]] std::optional<LockedPage> write(Transaction& tx,ddb::storage::PageId id){if(!locks_.lock_exclusive(tx,ResourceId(id)))return{};std::lock_guard guard(pool_mutex_);auto* p=pool_.fetch_page(id);if(!p)throw std::runtime_error("cannot pin write page");return LockedPage(pool_,pool_mutex_,id,p,true);}
 private: ddb::buffer::BufferPoolManager& pool_;LockManager& locks_;std::mutex pool_mutex_;
};
class TransactionalTableHeap final {
 public:
  TransactionalTableHeap(ddb::execution::TableHeap& heap,LockManager& locks):heap_(heap),locks_(locks){}
  [[nodiscard]] std::optional<ddb::execution::Tuple> get(Transaction& tx,ddb::execution::RecordId id)const{if(!locks_.lock_shared(tx,ResourceId(id.page_id)))return{};return heap_.get(id);}
  [[nodiscard]] std::optional<ddb::execution::RecordId> insert(Transaction& tx,const ddb::execution::Tuple& tuple){if(!locks_.lock_exclusive(tx,ResourceId(heap_.metadata_page_id())))return{};return heap_.insert(tuple,&tx.mutation_context(heap_.buffer_pool_manager()));}
  [[nodiscard]] bool erase(Transaction& tx,ddb::execution::RecordId id){return locks_.lock_exclusive(tx,ResourceId(id.page_id))&&heap_.erase(id,&tx.mutation_context(heap_.buffer_pool_manager()));}
 private: ddb::execution::TableHeap& heap_;LockManager& locks_;
};
class TransactionalBPlusTree final {
 public:
  TransactionalBPlusTree(ddb::index::BPlusTree& tree,LockManager& locks):tree_(tree),locks_(locks){}
  [[nodiscard]] bool get_value(Transaction& tx,ddb::index::KeyType key,ddb::index::RecordId& out)const{return locks_.lock_shared(tx,ResourceId(tree_.metadata_page_id()))&&tree_.get_value(key,out);}
  [[nodiscard]] bool insert(Transaction& tx,ddb::index::KeyType key,ddb::index::RecordId value){return locks_.lock_exclusive(tx,ResourceId(tree_.metadata_page_id()))&&tree_.insert(key,value,&tx.mutation_context(tree_.buffer_pool_manager()));}
  [[nodiscard]] bool remove(Transaction& tx,ddb::index::KeyType key){return locks_.lock_exclusive(tx,ResourceId(tree_.metadata_page_id()))&&tree_.remove(key,&tx.mutation_context(tree_.buffer_pool_manager()));}
 private: ddb::index::BPlusTree& tree_;LockManager& locks_;
};
}
