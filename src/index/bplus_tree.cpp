#include "ddb/index/bplus_tree.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <set>
#include <stdexcept>

namespace ddb::index {
namespace {
using ddb::storage::Page;
using ddb::storage::PageId;
constexpr std::uint64_t kInvalid = PageId::kInvalidValue;
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kLeafEntrySize = 20;
constexpr std::size_t kInternalEntrySize = 16;
constexpr std::array<std::byte, 8> kMetadataMagic{std::byte{'D'},std::byte{'D'},std::byte{'B'},std::byte{'B'},std::byte{'P'},std::byte{'T'},std::byte{'3'},std::byte{0}};

void put_u64(std::byte* out, std::size_t at, std::uint64_t value) { for (unsigned i=0;i<8;++i) out[at+i]=std::byte{static_cast<unsigned char>((value>>(i*8U))&0xffU)}; }
std::uint64_t get_u64(const std::byte* in, std::size_t at) { std::uint64_t value=0; for(unsigned i=0;i<8;++i) value|=static_cast<std::uint64_t>(std::to_integer<unsigned char>(in[at+i]))<<(i*8U); return value; }
void put_u32(std::byte* out, std::size_t at, std::uint32_t value) { for (unsigned i=0;i<4;++i) out[at+i]=std::byte{static_cast<unsigned char>((value>>(i*8U))&0xffU)}; }
std::uint32_t get_u32(const std::byte* in, std::size_t at) { std::uint32_t value=0; for(unsigned i=0;i<4;++i) value|=static_cast<std::uint32_t>(std::to_integer<unsigned char>(in[at+i]))<<(i*8U); return value; }
void put_u16(std::byte* out, std::size_t at, std::uint16_t value) { out[at]=std::byte{static_cast<unsigned char>(value&0xffU)}; out[at+1]=std::byte{static_cast<unsigned char>(value>>8U)}; }
std::uint16_t get_u16(const std::byte* in, std::size_t at) { return static_cast<std::uint16_t>(std::to_integer<unsigned char>(in[at])) | static_cast<std::uint16_t>(std::to_integer<unsigned char>(in[at+1])<<8U); }
PageId page_id(std::uint64_t value) { return PageId(value); }
}

struct BPlusTree::Node final {
  bool leaf{true}; PageId parent{}; PageId next{};
  std::vector<KeyType> keys; std::vector<RecordId> values; std::vector<PageId> children;
};

PageId BPlusTree::create(ddb::storage::PageManager& pm, ddb::buffer::BufferPoolManager& bp, BPlusTreeConfig config) {
  if (config.leaf_max_keys < 2 || config.internal_max_keys < 2 ||
      kHeaderSize + static_cast<std::size_t>(config.leaf_max_keys) * kLeafEntrySize > ddb::storage::kPageSize ||
      kHeaderSize + 8 + static_cast<std::size_t>(config.internal_max_keys) * kInternalEntrySize > ddb::storage::kPageSize) throw std::invalid_argument("invalid B+ tree node capacity");
  const PageId metadata = pm.allocate_page();
  const PageId root = pm.allocate_page();
  Page* meta = bp.fetch_page(metadata); if (!meta) throw std::runtime_error("cannot fetch new B+ tree metadata");
  meta->clear(); std::copy(kMetadataMagic.begin(),kMetadataMagic.end(),meta->data()); put_u64(meta->data(),8,root.value()); put_u16(meta->data(),16,config.leaf_max_keys); put_u16(meta->data(),18,config.internal_max_keys); (void)bp.unpin_page(metadata,true);
  BPlusTree tree(pm, bp, metadata);
  Node empty; empty.leaf = true; empty.parent = PageId{}; empty.next = PageId{};
  tree.write_node(root, empty); tree.write_metadata(); tree.flush();
  return metadata;
}

BPlusTree::BPlusTree(ddb::storage::PageManager& pm, ddb::buffer::BufferPoolManager& bp, PageId metadata)
    : page_manager_(pm), buffer_pool_(bp), metadata_page_id_(metadata), root_page_id_(), config_() {
  Page* page = buffer_pool_.fetch_page(metadata_page_id_);
  if (page == nullptr) throw std::runtime_error("cannot fetch B+ tree metadata page");
  const std::byte* data = page->data();
  const bool valid = std::equal(kMetadataMagic.begin(), kMetadataMagic.end(), data);
  if (valid) { root_page_id_ = page_id(get_u64(data, 8)); config_.leaf_max_keys=get_u16(data,16); config_.internal_max_keys=get_u16(data,18); }
  (void)buffer_pool_.unpin_page(metadata_page_id_, false);
  if (!valid || !root_page_id_.is_valid() || config_.leaf_max_keys < 2 || config_.internal_max_keys < 2) throw std::runtime_error("invalid B+ tree metadata page");
}

void BPlusTree::write_metadata() {
  Page* page=buffer_pool_.fetch_page(metadata_page_id_); if(!page) throw std::runtime_error("cannot fetch B+ tree metadata");
  page->clear(); std::copy(kMetadataMagic.begin(),kMetadataMagic.end(),page->data()); put_u64(page->data(),8,root_page_id_.value()); put_u16(page->data(),16,config_.leaf_max_keys); put_u16(page->data(),18,config_.internal_max_keys);
  (void)buffer_pool_.unpin_page(metadata_page_id_,true);
}

BPlusTree::Node BPlusTree::read_node(PageId id) const {
  Page* page=buffer_pool_.fetch_page(id); if(!page) throw std::runtime_error("cannot fetch B+ tree node");
  const std::byte* d=page->data(); Node node; node.leaf=std::to_integer<unsigned char>(d[0])==1U; node.parent=page_id(get_u64(d,4)); node.next=page_id(get_u64(d,12)); const auto count=get_u16(d,20);
  const std::size_t max=node.leaf?config_.leaf_max_keys:config_.internal_max_keys;
  if ((std::to_integer<unsigned char>(d[0])!=1U && std::to_integer<unsigned char>(d[0])!=2U) || count>max) { (void)buffer_pool_.unpin_page(id,false); throw std::runtime_error("corrupt B+ tree node"); }
  if(node.leaf) { for(std::size_t i=0;i<count;++i){const auto at=kHeaderSize+i*kLeafEntrySize; node.keys.push_back(std::bit_cast<KeyType>(get_u64(d,at))); node.values.push_back({page_id(get_u64(d,at+8)),get_u32(d,at+16)});} }
  else { node.children.push_back(page_id(get_u64(d,kHeaderSize))); for(std::size_t i=0;i<count;++i){const auto at=kHeaderSize+8+i*kInternalEntrySize; node.keys.push_back(std::bit_cast<KeyType>(get_u64(d,at))); node.children.push_back(page_id(get_u64(d,at+8)));} }
  (void)buffer_pool_.unpin_page(id,false); return node;
}

void BPlusTree::write_node(PageId id,const Node& n) {
  if(n.keys.size()>(n.leaf?config_.leaf_max_keys:config_.internal_max_keys) || (n.leaf?n.values.size()!=n.keys.size():n.children.size()!=n.keys.size()+1)) throw std::logic_error("invalid B+ tree node write");
  Page* p=buffer_pool_.fetch_page(id); if(!p) throw std::runtime_error("cannot fetch B+ tree node for write"); p->clear(); std::byte* d=p->data(); d[0]=std::byte{static_cast<unsigned char>(n.leaf?1:2)}; put_u64(d,4,n.parent.value()); put_u64(d,12,n.next.value()); put_u16(d,20,static_cast<std::uint16_t>(n.keys.size()));
  if(n.leaf) for(std::size_t i=0;i<n.keys.size();++i){const auto at=kHeaderSize+i*kLeafEntrySize; put_u64(d,at,std::bit_cast<std::uint64_t>(n.keys[i])); put_u64(d,at+8,n.values[i].page_id.value()); put_u32(d,at+16,n.values[i].slot_id);}
  else {put_u64(d,kHeaderSize,n.children[0].value()); for(std::size_t i=0;i<n.keys.size();++i){const auto at=kHeaderSize+8+i*kInternalEntrySize; put_u64(d,at,std::bit_cast<std::uint64_t>(n.keys[i])); put_u64(d,at+8,n.children[i+1].value());}}
  (void)buffer_pool_.unpin_page(id,true);
}

PageId BPlusTree::allocate_node(Node node) { const PageId id=page_manager_.allocate_page(); write_node(id,node); return id; }
PageId BPlusTree::find_leaf(KeyType key) const { PageId id=root_page_id_; for(;;){Node n=read_node(id); if(n.leaf)return id; id=n.children[static_cast<std::size_t>(std::upper_bound(n.keys.begin(),n.keys.end(),key)-n.keys.begin())];} }
bool BPlusTree::get_value(KeyType key,RecordId& out) const { Node leaf=read_node(find_leaf(key)); const auto it=std::lower_bound(leaf.keys.begin(),leaf.keys.end(),key); if(it==leaf.keys.end()||*it!=key)return false; out=leaf.values[static_cast<std::size_t>(it-leaf.keys.begin())]; return true; }

void BPlusTree::set_parent(PageId child,PageId parent){Node n=read_node(child); n.parent=parent; write_node(child,n);}
void BPlusTree::insert_into_parent(PageId left,KeyType sep,PageId right) {
  Node left_node=read_node(left); if(!left_node.parent.is_valid()) { Node root; root.leaf=false; root.parent=PageId{}; root.children={left,right}; root.keys={sep}; const PageId rid=allocate_node(root); set_parent(left,rid); set_parent(right,rid); root_page_id_=rid; write_metadata(); return; }
  const PageId pid=left_node.parent; Node parent=read_node(pid); const auto pos=std::find(parent.children.begin(),parent.children.end(),left); if(pos==parent.children.end())throw std::logic_error("parent missing child"); const auto idx=static_cast<std::size_t>(pos-parent.children.begin()); parent.keys.insert(parent.keys.begin()+static_cast<std::ptrdiff_t>(idx),sep); parent.children.insert(parent.children.begin()+static_cast<std::ptrdiff_t>(idx+1),right); set_parent(right,pid);
  if(parent.keys.size()<=config_.internal_max_keys){write_node(pid,parent);return;}
  const std::size_t mid=parent.keys.size()/2; const KeyType promote=parent.keys[mid]; Node sibling; sibling.leaf=false; sibling.parent=parent.parent; sibling.keys.assign(parent.keys.begin()+static_cast<std::ptrdiff_t>(mid+1),parent.keys.end()); sibling.children.assign(parent.children.begin()+static_cast<std::ptrdiff_t>(mid+1),parent.children.end()); parent.keys.resize(mid); parent.children.resize(mid+1); const PageId sid=allocate_node(sibling); write_node(pid,parent); for(PageId child:sibling.children)set_parent(child,sid); insert_into_parent(pid,promote,sid);
}

bool BPlusTree::insert(KeyType key,RecordId value) { const PageId id=find_leaf(key); Node leaf=read_node(id); const auto it=std::lower_bound(leaf.keys.begin(),leaf.keys.end(),key); const auto at=static_cast<std::size_t>(it-leaf.keys.begin()); if(it!=leaf.keys.end()&&*it==key)return false; leaf.keys.insert(it,key); leaf.values.insert(leaf.values.begin()+static_cast<std::ptrdiff_t>(at),value); if(leaf.keys.size()<=config_.leaf_max_keys){write_node(id,leaf); if(at==0)update_parent_separator(id,key); return true;} const std::size_t split=leaf.keys.size()/2; Node right; right.leaf=true; right.parent=leaf.parent; right.next=leaf.next; right.keys.assign(leaf.keys.begin()+static_cast<std::ptrdiff_t>(split),leaf.keys.end()); right.values.assign(leaf.values.begin()+static_cast<std::ptrdiff_t>(split),leaf.values.end()); leaf.keys.resize(split); leaf.values.resize(split); const PageId rid=allocate_node(right); leaf.next=rid; write_node(id,leaf); insert_into_parent(id,right.keys.front(),rid); return true; }

void BPlusTree::update_parent_separator(PageId child,KeyType first){
  Node n=read_node(child); if(!n.parent.is_valid())return;
  Node p=read_node(n.parent); const auto pos=std::find(p.children.begin(),p.children.end(),child);
  if(pos==p.children.end()) throw std::logic_error("parent missing child during separator update");
  const auto idx=static_cast<std::size_t>(pos-p.children.begin());
  if(idx>0){p.keys[idx-1]=first;write_node(n.parent,p);}
  else { update_parent_separator(n.parent,first); }
}
bool BPlusTree::remove(KeyType key) { const PageId id=find_leaf(key); Node leaf=read_node(id); const auto it=std::lower_bound(leaf.keys.begin(),leaf.keys.end(),key); if(it==leaf.keys.end()||*it!=key)return false; const auto idx=static_cast<std::size_t>(it-leaf.keys.begin()); leaf.keys.erase(it); leaf.values.erase(leaf.values.begin()+static_cast<std::ptrdiff_t>(idx)); if(id==root_page_id_){write_node(id,leaf);return true;} if(leaf.keys.size()>=(config_.leaf_max_keys+1)/2){write_node(id,leaf);if(idx==0&&!leaf.keys.empty())update_parent_separator(id,leaf.keys.front());return true;} write_node(id,leaf); rebalance_leaf(id);return true; }

void BPlusTree::rebalance_leaf(PageId id){Node n=read_node(id); Node p=read_node(n.parent); const auto pos=std::find(p.children.begin(),p.children.end(),id); const auto idx=static_cast<std::size_t>(pos-p.children.begin()); const std::size_t min=(config_.leaf_max_keys+1)/2; if(idx>0){const PageId lid=p.children[idx-1];Node l=read_node(lid);if(l.keys.size()>min){n.keys.insert(n.keys.begin(),l.keys.back());n.values.insert(n.values.begin(),l.values.back());l.keys.pop_back();l.values.pop_back();p.keys[idx-1]=n.keys.front();write_node(lid,l);write_node(id,n);write_node(n.parent,p);return;} l.keys.insert(l.keys.end(),n.keys.begin(),n.keys.end());l.values.insert(l.values.end(),n.values.begin(),n.values.end());l.next=n.next;p.keys.erase(p.keys.begin()+static_cast<std::ptrdiff_t>(idx-1));p.children.erase(p.children.begin()+static_cast<std::ptrdiff_t>(idx));write_node(lid,l);write_node(n.parent,p);rebalance_internal(n.parent);return;} const PageId rid=p.children[idx+1];Node r=read_node(rid);if(r.keys.size()>min){n.keys.push_back(r.keys.front());n.values.push_back(r.values.front());r.keys.erase(r.keys.begin());r.values.erase(r.values.begin());p.keys[idx]=r.keys.front();write_node(id,n);write_node(rid,r);write_node(n.parent,p);update_parent_separator(n.parent,n.keys.front());return;} n.keys.insert(n.keys.end(),r.keys.begin(),r.keys.end());n.values.insert(n.values.end(),r.values.begin(),r.values.end());n.next=r.next;p.keys.erase(p.keys.begin()+static_cast<std::ptrdiff_t>(idx));p.children.erase(p.children.begin()+static_cast<std::ptrdiff_t>(idx+1));write_node(id,n);write_node(n.parent,p);update_parent_separator(n.parent,n.keys.front());rebalance_internal(n.parent);}

void BPlusTree::rebalance_internal(PageId id){Node n=read_node(id);if(id==root_page_id_){if(n.keys.empty()){const PageId child=n.children.front();set_parent(child,PageId{});root_page_id_=child;write_metadata();}else write_node(id,n);return;} const std::size_t min=config_.internal_max_keys/2;if(n.keys.size()>=min){write_node(id,n);return;}Node p=read_node(n.parent);const auto pos=std::find(p.children.begin(),p.children.end(),id);const auto idx=static_cast<std::size_t>(pos-p.children.begin());if(idx>0){const PageId lid=p.children[idx-1];Node l=read_node(lid);if(l.keys.size()>min){n.keys.insert(n.keys.begin(),p.keys[idx-1]);n.children.insert(n.children.begin(),l.children.back());p.keys[idx-1]=l.keys.back();l.keys.pop_back();const PageId moved=l.children.back();l.children.pop_back();set_parent(moved,id);write_node(lid,l);write_node(id,n);write_node(n.parent,p);return;}l.keys.push_back(p.keys[idx-1]);l.keys.insert(l.keys.end(),n.keys.begin(),n.keys.end());l.children.insert(l.children.end(),n.children.begin(),n.children.end());for(PageId c:n.children)set_parent(c,lid);p.keys.erase(p.keys.begin()+static_cast<std::ptrdiff_t>(idx-1));p.children.erase(p.children.begin()+static_cast<std::ptrdiff_t>(idx));write_node(lid,l);write_node(n.parent,p);rebalance_internal(n.parent);return;}const PageId rid=p.children[idx+1];Node r=read_node(rid);if(r.keys.size()>min){n.keys.push_back(p.keys[idx]);n.children.push_back(r.children.front());p.keys[idx]=r.keys.front();const PageId moved=r.children.front();r.children.erase(r.children.begin());r.keys.erase(r.keys.begin());set_parent(moved,id);write_node(id,n);write_node(rid,r);write_node(n.parent,p);return;}n.keys.push_back(p.keys[idx]);n.keys.insert(n.keys.end(),r.keys.begin(),r.keys.end());n.children.insert(n.children.end(),r.children.begin(),r.children.end());for(PageId c:r.children)set_parent(c,id);p.keys.erase(p.keys.begin()+static_cast<std::ptrdiff_t>(idx));p.children.erase(p.children.begin()+static_cast<std::ptrdiff_t>(idx+1));write_node(id,n);write_node(n.parent,p);rebalance_internal(n.parent);}

std::vector<std::pair<KeyType,RecordId>> BPlusTree::scan(KeyType low,KeyType high) const {std::vector<std::pair<KeyType,RecordId>> out;if(low>high)return out;PageId id=find_leaf(low);while(id.is_valid()){Node n=read_node(id);for(std::size_t i=0;i<n.keys.size();++i){if(n.keys[i]<low)continue;if(n.keys[i]>high)return out;out.emplace_back(n.keys[i],n.values[i]);}id=n.next;}return out;}
void BPlusTree::flush(){buffer_pool_.flush_all_pages();}
std::size_t BPlusTree::height() const {std::size_t h=1;Node n=read_node(root_page_id_);while(!n.leaf){++h;n=read_node(n.children.front());}return h;}
bool BPlusTree::validate_invariants() const noexcept {
  try {
    auto first_key = [&](PageId id) { Node n=read_node(id); while(!n.leaf) { id=n.children.front(); n=read_node(id); } if(n.keys.empty()) throw std::runtime_error("empty child"); return n.keys.front(); };
    std::set<std::uint64_t> seen; std::vector<PageId> leaves; std::set<KeyType> unique;
    auto walk = [&](auto&& self, PageId id, PageId parent)->bool {
      if(!id.is_valid() || seen.contains(id.value())) return false;
      seen.insert(id.value()); Node n=read_node(id);
      if(n.parent!=parent || !std::is_sorted(n.keys.begin(),n.keys.end())) return false;
      if(n.leaf) { for(KeyType k:n.keys) if(!unique.insert(k).second) return false; leaves.push_back(id); return true; }
      if(n.children.size()!=n.keys.size()+1) return false;
      for(std::size_t i=0;i<n.keys.size();++i) if(first_key(n.children[i+1])!=n.keys[i]) return false;
      for(PageId child:n.children) if(!self(self,child,id)) return false;
      return true;
    };
    if(!walk(walk,root_page_id_,PageId{})) return false;
    PageId next=leaves.empty()?PageId{}:leaves.front();
    for(PageId expected:leaves){if(next!=expected)return false;next=read_node(next).next;}
    return !next.is_valid() && buffer_pool_.validate_invariants();
  } catch (...) { return false; }
}
}  // namespace ddb::index
