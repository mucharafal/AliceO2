// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file EncodedBlock.h
/// \brief Set of entropy-encoded blocks

///  Used to store a CTF of particular detector. Can be build as a flat buffer which can be directly messaged between DPL devices

#ifndef ALICEO2_ENCODED_BLOCKS_H
#define ALICEO2_ENCODED_BLOCKS_H
#undef NDEBUG
#include <cassert>
#include <type_traits>
#include <cstddef>
#include <Rtypes.h>
#include "rANS/rans.h"
#include "rANS/utils.h"
#include "TTree.h"
#include "CommonUtils/StringUtils.h"
#include "Framework/Logger.h"
#include "DetectorsCommonDataFormats/CTFDictHeader.h"

namespace o2
{
namespace ctf
{

namespace detail
{

template <class, class Enable = void>
struct is_iterator : std::false_type {
};

template <class T>
struct is_iterator<T, std::enable_if_t<
                        std::is_base_of_v<std::input_iterator_tag, typename std::iterator_traits<T>::iterator_category> ||
                        std::is_same_v<std::output_iterator_tag, typename std::iterator_traits<T>::iterator_category>>>
  : std::true_type {
};

template <class T>
inline constexpr bool is_iterator_v = is_iterator<T>::value;
} // namespace detail

using namespace o2::rans;
constexpr size_t Alignment = 16;

constexpr int WrappersSplitLevel = 99;
constexpr int WrappersCompressionLevel = 1;

/// This is the type of the vector to be used for the EncodedBlocks buffer allocation
using BufferType = uint8_t; // to avoid every detector using different types, we better define it here

/// align size to given diven number of bytes
inline size_t alignSize(size_t sizeBytes)
{
  auto res = sizeBytes % Alignment;
  return res ? sizeBytes + (Alignment - res) : sizeBytes;
}

/// relocate pointer by the difference of addresses
template <class T>
inline T* relocatePointer(const char* oldBase, char* newBase, const T* ptr)
{
  return (ptr != nullptr) ? reinterpret_cast<T*>(newBase + (reinterpret_cast<const char*>(ptr) - oldBase)) : nullptr;
}

template <typename source_T, typename dest_T, std::enable_if_t<(sizeof(dest_T) >= sizeof(source_T)), bool> = true>
inline size_t calculateNDestTElements(size_t sourceElems) noexcept
{
  const size_t sizeOfSourceArray = sourceElems * sizeof(source_T);
  return sizeOfSourceArray / sizeof(dest_T) + (sizeOfSourceArray % sizeof(dest_T) != 0);
};

template <typename source_T, typename dest_T, std::enable_if_t<(sizeof(dest_T) >= sizeof(source_T)), bool> = true>
inline size_t calculatePaddedSize(size_t nElems) noexcept
{
  const size_t sizeOfSourceT = sizeof(source_T);
  const size_t sizeOfDestT = sizeof(dest_T);

  // this is equivalent to (sizeOfSourceT / sizeOfDestT) * std::ceil(sizeOfSourceArray/ sizeOfDestT)
  return (sizeOfDestT / sizeOfSourceT) * calculateNDestTElements<source_T, dest_T>(nElems);
};

///>>======================== Auxiliary classes =======================>>

struct ANSHeader {
  uint8_t majorVersion;
  uint8_t minorVersion;

  void clear() { majorVersion = minorVersion = 0; }
  ClassDefNV(ANSHeader, 1);
};

struct Metadata {
  enum class OptStore : uint8_t { // describe how the store the data described by this metadata
    EENCODE,                      // entropy encoding applied
    ROOTCompression,              // original data repacked to array with slot-size = streamSize and saved with root compression
    NONE,                         // original data repacked to array with slot-size = streamSize and saved w/o compression
    NODATA                        // no data was provided
  };
  size_t messageLength = 0;
  size_t nLiterals = 0;
  uint8_t messageWordSize = 0;
  uint8_t coderType = 0;
  uint8_t streamSize = 0;
  uint8_t probabilityBits = 0;
  OptStore opt = OptStore::EENCODE;
  int32_t min = 0;
  int32_t max = 0;
  int nDictWords = 0;
  int nDataWords = 0;
  int nLiteralWords = 0;

  void clear()
  {
    min = max = 0;
    messageLength = 0;
    messageWordSize = 0;
    nLiterals = 0;
    coderType = 0;
    streamSize = 0;
    probabilityBits = 0;
    nDictWords = 0;
    nDataWords = 0;
    nLiteralWords = 0;
  }
  ClassDefNV(Metadata, 2);
};

/// registry struct for the buffer start and offsets of writable space
struct Registry {
  char* head = nullptr;     //! pointer on the head of the CTF
  int nFilledBlocks = 0;    // number of filled blocks = next block to fill (must be strictly consecutive)
  size_t offsFreeStart = 0; //! offset of the start of the writable space (wrt head), in bytes!!!
  size_t size = 0;          // full size in bytes!!!

  /// calculate the pointer of the head of the writable space
  char* getFreeBlockStart() const
  {
    assert(offsFreeStart <= size);
    return head + offsFreeStart;
  }

  /// size in bytes available to fill data
  size_t getFreeSize() const
  {
    return size - offsFreeStart;
  }

  ClassDefNV(Registry, 1);
};

/// binary blob for single entropy-compressed column: metadata + (optional) dictionary and data buffer + their sizes
template <typename W = uint32_t>
struct Block {

  Registry* registry = nullptr; //! non-persistent info for in-memory ops
  int nDict = 0;                // dictionary length (if any)
  int nData = 0;                // length of data
  int nLiterals = 0;            // length of literals vector (if any)
  int nStored = 0;              // total length
  W* payload = nullptr;         //[nStored];

  inline const W* getDict() const { return nDict ? payload : nullptr; }
  inline const W* getData() const { return nData ? (payload + nDict) : nullptr; }
  inline const W* getDataPointer() const { return payload ? (payload + nDict) : nullptr; } // needed when nData is not set yet
  inline const W* getLiterals() const { return nLiterals ? (payload + nDict + nData) : nullptr; }

  inline W* getCreatePayload() { return payload ? payload : (registry ? (payload = reinterpret_cast<W*>(registry->getFreeBlockStart())) : nullptr); }
  inline W* getCreateDict() { return payload ? payload : getCreatePayload(); }
  inline W* getCreateData() { return payload ? (payload + nDict) : getCreatePayload(); }
  inline W* getCreateLiterals() { return payload ? payload + (nDict + nData) : getCreatePayload(); }

  inline auto getOffsDict() { return reinterpret_cast<std::uintptr_t>(getCreateDict()) - reinterpret_cast<std::uintptr_t>(registry->head); }
  inline auto getOffsData() { return reinterpret_cast<std::uintptr_t>(getCreateData()) - reinterpret_cast<std::uintptr_t>(registry->head); }
  inline auto getOffsLiterals() { return reinterpret_cast<std::uintptr_t>(getCreateLiterals()) - reinterpret_cast<std::uintptr_t>(registry->head); }

  inline void setNDict(int _ndict)
  {
    nDict = _ndict;
    nStored += nDict;
  }

  inline void setNData(int _ndata)
  {
    nData = _ndata;
    nStored += nData;
  }

  inline void setNLiterals(int _nliterals)
  {
    nLiterals = _nliterals;
    nStored += nLiterals;
  }

  inline int getNDict() const { return nDict; }
  inline int getNData() const { return nData; }
  inline int getNLiterals() const { return nLiterals; }
  inline int getNStored() const { return nStored; }

  ~Block()
  {
    if (!registry) { // this is a standalone block owning its data
      delete[] payload;
    }
  }

  /// clear itself
  void clear()
  {
    nDict = 0;
    nData = 0;
    nLiterals = 0;
    nStored = 0;
    payload = nullptr;
  }

  /// estimate free size needed to add new block
  static size_t estimateSize(int n)
  {
    return alignSize(n * sizeof(W));
  }

  // store a dictionary in an empty block
  void storeDict(int _ndict, const W* _dict)
  {
    if (getNStored() > 0) {
      throw std::runtime_error("trying to write in occupied block");
    }
    size_t sz = estimateSize(_ndict);
    assert(registry); // this method is valid only for flat version, which has a registry
    assert(sz <= registry->getFreeSize());
    assert((_ndict > 0) == (_dict != nullptr));
    setNDict(_ndict);
    if (nDict) {
      memcpy(getCreateDict(), _dict, _ndict * sizeof(W));
      realignBlock();
    }
  };

  // store a dictionary to a block which can either be empty or contain a dict.
  void storeData(int _ndata, const W* _data)
  {
    if (getNStored() > getNDict()) {
      throw std::runtime_error("trying to write in occupied block");
    }

    size_t sz = estimateSize(_ndata);
    assert(registry); // this method is valid only for flat version, which has a registry
    assert(sz <= registry->getFreeSize());
    assert((_ndata > 0) == (_data != nullptr));
    setNData(_ndata);
    if (nData) {
      memcpy(getCreateData(), _data, _ndata * sizeof(W));
      realignBlock();
    }
  }

  // store a dictionary to a block which can either be empty or contain a dict.
  void storeLiterals(int _nliterals, const W* _literals)
  {
    if (getNStored() > getNDict() + getNData()) {
      throw std::runtime_error("trying to write in occupied block");
    }

    size_t sz = estimateSize(_nliterals);
    assert(registry); // this method is valid only for flat version, which has a registry
    assert(sz <= registry->getFreeSize());
    //    assert((_nliterals > 0) == (_literals != nullptr));
    setNLiterals(_nliterals);
    if (nLiterals) {
      memcpy(getCreateLiterals(), _literals, _nliterals * sizeof(W));
      realignBlock();
    }
  }

  // resize block and free up unused buffer space.
  void realignBlock()
  {
    size_t sz = estimateSize(getNStored());
    registry->offsFreeStart = (reinterpret_cast<char*>(payload) - registry->head) + sz;
  }

  /// store binary blob data (buffer filled from head to tail)
  void store(int _ndict, int _ndata, int _nliterals, const W* _dict, const W* _data, const W* _literals)
  {
    size_t sz = estimateSize(_ndict + _ndata + _nliterals);
    assert(registry); // this method is valid only for flat version, which has a registry
    assert(sz <= registry->getFreeSize());
    assert((_ndict > 0) == (_dict != nullptr));
    assert((_ndata > 0) == (_data != nullptr));
    //    assert(_literals == _data + _nliterals);
    setNDict(_ndict);
    setNData(_ndata);
    setNLiterals(_nliterals);
    getCreatePayload(); // do this even for empty block!!!
    if (getNStored()) {
      payload = reinterpret_cast<W*>(registry->getFreeBlockStart());
      if (getNDict()) {
        memcpy(getCreateDict(), _dict, _ndict * sizeof(W));
      }
      if (getNData()) {
        memcpy(getCreateData(), _data, _ndata * sizeof(W));
      }
      if (getNLiterals()) {
        memcpy(getCreateLiterals(), _literals, _nliterals * sizeof(W));
      }
    }
    realignBlock();
  }

  /// relocate to different head position
  void relocate(const char* oldHead, char* newHeadData, char* newHeadRegistry)
  {
    payload = relocatePointer(oldHead, newHeadData, payload);
    registry = relocatePointer(oldHead, newHeadRegistry, registry);
  }

  ClassDefNV(Block, 1);
}; // namespace ctf

///<<======================== Auxiliary classes =======================<<

template <typename H, int N, typename W = uint32_t>
class EncodedBlocks
{
 public:
  typedef EncodedBlocks<H, N, W> base;

  void setHeader(const H& h) { mHeader = h; }
  const H& getHeader() const { return mHeader; }
  H& getHeader() { return mHeader; }
  std::shared_ptr<H> cloneHeader() const { return std::shared_ptr<H>(new H(mHeader)); } // for dictionary creation

  const auto& getRegistry() const { return mRegistry; }

  const auto& getMetadata() const { return mMetadata; }

  auto& getMetadata(int i) const
  {
    assert(i < N);
    return mMetadata[i];
  }

  auto& getBlock(int i) const
  {
    assert(i < N);
    return mBlocks[i];
  }

  auto getFrequencyTable(int i) const
  {
    o2::rans::FrequencyTable ft;
    const auto& bl = getBlock(i);
    const auto& md = getMetadata(i);
    ft.addFrequencies(bl.getDict(), bl.getDict() + bl.getNDict(), md.min, md.max);
    return ft;
  }

  void setANSHeader(const ANSHeader& h) { mANSHeader = h; }
  const ANSHeader& getANSHeader() const { return mANSHeader; }
  ANSHeader& getANSHeader() { return mANSHeader; }

  static constexpr int getNBlocks() { return N; }

  static size_t getMinAlignedSize() { return alignSize(sizeof(base)); }

  /// cast arbitrary buffer head to container class. Head is supposed to respect the alignment
  static auto get(void* head) { return reinterpret_cast<EncodedBlocks*>(head); }
  static const auto get(const void* head) { return reinterpret_cast<const EncodedBlocks*>(head); }

  /// get const image of the container wrapper, with pointers in the image relocated to new head
  static const auto getImage(const void* newHead);

  /// create container from arbitrary buffer of predefined size (in bytes!!!). Head is supposed to respect the alignment
  static auto create(void* head, size_t sz);

  /// create container from vector. Head is supposed to respect the alignment
  template <typename VD>
  static auto create(VD& v);

  /// estimate free size needed to add new block
  static size_t estimateBlockSize(int n) { return Block<W>::estimateSize(n); }

  /// check if empty and valid
  bool empty() const { return (mRegistry.offsFreeStart == alignSize(sizeof(*this))) && (mRegistry.size >= mRegistry.offsFreeStart); }

  /// check if flat and valid
  bool flat() const { return mRegistry.size > 0 && (mRegistry.size >= mRegistry.offsFreeStart) && (mBlocks[0].registry == &mRegistry) && (mBlocks[N - 1].registry == &mRegistry); }

  /// clear itself
  void clear();

  /// Compactify by eliminating empty space
  size_t compactify() { return (mRegistry.size = estimateSize()); }

  /// total allocated size in bytes
  size_t size() const { return mRegistry.size; }

  /// size remaining for additional data
  size_t getFreeSize() const { return mRegistry.getFreeSize(); }

  /// expand the storage to new size in bytes
  template <typename buffer_T>
  static auto expand(buffer_T& buffer, size_t newsizeBytes);

  /// copy itself to flat buffer created on the fly from the vector
  template <typename V>
  void copyToFlat(V& vec);

  /// copy itself to flat buffer created on the fly at the provided pointer. The destination block should be at least of size estimateSize()
  void copyToFlat(void* base) { fillFlatCopy(create(base, estimateSize())); }

  /// attach to tree
  size_t appendToTree(TTree& tree, const std::string& name) const;

  /// read from tree to non-flat object
  void readFromTree(TTree& tree, const std::string& name, int ev = 0);

  /// read from tree to destination buffer vector
  template <typename VD>
  static void readFromTree(VD& vec, TTree& tree, const std::string& name, int ev = 0);

  /// encode vector src to bloc at provided slot
  template <typename VE, typename buffer_T>
  inline void encode(const VE& src, int slot, uint8_t symbolTablePrecision, Metadata::OptStore opt, buffer_T* buffer = nullptr, const void* encoderExt = nullptr, float memfc = 1.f)
  {
    encode(std::begin(src), std::end(src), slot, symbolTablePrecision, opt, buffer, encoderExt, memfc);
  }

  /// encode vector src to bloc at provided slot
  template <typename input_IT, typename buffer_T>
  void encode(const input_IT srcBegin, const input_IT srcEnd, int slot, uint8_t symbolTablePrecision, Metadata::OptStore opt, buffer_T* buffer = nullptr, const void* encoderExt = nullptr, float memfc = 1.f);

  /// decode block at provided slot to destination vector (will be resized as needed)
  template <class container_T, class container_IT = typename container_T::iterator>
  void decode(container_T& dest, int slot, const void* decoderExt = nullptr) const;

  /// decode block at provided slot to destination pointer, the needed space assumed to be available
  template <typename D_IT, std::enable_if_t<detail::is_iterator_v<D_IT>, bool> = true>
  void decode(D_IT dest, int slot, const void* decoderExt = nullptr) const;

  /// create a special EncodedBlocks containing only dictionaries made from provided vector of frequency tables
  static std::vector<char> createDictionaryBlocks(const std::vector<o2::rans::FrequencyTable>& vfreq, const std::vector<Metadata>& prbits);

  /// print itself
  void print(const std::string& prefix = "", int verbosity = 1) const;
  void dump(const std::string& prefix = "", int ncol = 20) const;

 protected:
  static_assert(N > 0, "number of encoded blocks < 1");

  Registry mRegistry;                //
  ANSHeader mANSHeader;              //  ANS header
  H mHeader;                         //  detector specific header
  std::array<Metadata, N> mMetadata; //  compressed block's details
  std::array<Block<W>, N> mBlocks;   //! this is in fact stored, but to overcome TBuffer limits we have to define the branches per block!!!

  /// setup internal structure and registry for given buffer size (in bytes!!!)
  void init(size_t sz);

  /// relocate to different head position, newHead points on start of the dynamic buffer holding the data.
  /// the address of the static part might be actually different (wrapper). This different newHead and
  /// wrapper addresses must be used when the buffer pointed by newHead is const (e.g. received from the
  /// DPL input), in this case we create a wrapper, which points on these const data
  static void relocate(const char* oldHead, char* newHead, char* wrapper, size_t newsize = 0);

  /// Estimate size of the buffer needed to store all compressed data in a contiguous block of memory, accounting for the alignment
  /// This method is to be called after reading object from the tree as a non-flat object!
  size_t estimateSize() const;

  /// do the same using metadata info
  size_t estimateSizeFromMetadata() const;

  /// Create its own flat copy in the destination empty flat object
  void fillFlatCopy(EncodedBlocks& dest) const;

  /// add and fill single branch
  template <typename D>
  static size_t fillTreeBranch(TTree& tree, const std::string& brname, D& dt, int compLevel, int splitLevel = 99);

  /// read single branch
  template <typename D>
  static bool readTreeBranch(TTree& tree, const std::string& brname, D& dt, int ev = 0);

  ClassDefNV(EncodedBlocks, 2);
};

///_____________________________________________________________________________
/// read from tree to non-flat object
template <typename H, int N, typename W>
void EncodedBlocks<H, N, W>::readFromTree(TTree& tree, const std::string& name, int ev)
{
  readTreeBranch(tree, o2::utils::Str::concat_string(name, "_wrapper."), *this, ev);
  for (int i = 0; i < N; i++) {
    readTreeBranch(tree, o2::utils::Str::concat_string(name, "_block.", std::to_string(i), "."), mBlocks[i], ev);
  }
}

///_____________________________________________________________________________
/// read from tree to destination buffer vector
template <typename H, int N, typename W>
template <typename VD>
void EncodedBlocks<H, N, W>::readFromTree(VD& vec, TTree& tree, const std::string& name, int ev)
{
  auto tmp = create(vec);
  if (!readTreeBranch(tree, o2::utils::Str::concat_string(name, "_wrapper."), *tmp, ev)) {
    throw std::runtime_error(fmt::format("Failed to read CTF header for {}", name));
  }
  tmp = tmp->expand(vec, tmp->estimateSizeFromMetadata());
  const auto& meta = tmp->getMetadata();
  for (int i = 0; i < N; i++) {
    Block<W> bl;
    readTreeBranch(tree, o2::utils::Str::concat_string(name, "_block.", std::to_string(i), "."), bl, ev);
    assert(meta[i].nDictWords == bl.getNDict());
    assert(meta[i].nDataWords == bl.getNData());
    assert(meta[i].nLiteralWords == bl.getNLiterals());
    tmp->mBlocks[i].store(bl.getNDict(), bl.getNData(), bl.getNLiterals(), bl.getDict(), bl.getData(), bl.getLiterals());
  }
}

///_____________________________________________________________________________
/// attach to tree
template <typename H, int N, typename W>
size_t EncodedBlocks<H, N, W>::appendToTree(TTree& tree, const std::string& name) const
{
  long s = 0;
  s += fillTreeBranch(tree, o2::utils::Str::concat_string(name, "_wrapper."), const_cast<base&>(*this), WrappersCompressionLevel, WrappersSplitLevel);
  for (int i = 0; i < N; i++) {
    int compression = mMetadata[i].opt == Metadata::OptStore::ROOTCompression ? 1 : 0;
    s += fillTreeBranch(tree, o2::utils::Str::concat_string(name, "_block.", std::to_string(i), "."), const_cast<Block<W>&>(mBlocks[i]), compression);
  }
  tree.SetEntries(tree.GetEntries() + 1);
  return s;
}

///_____________________________________________________________________________
/// read single branch
template <typename H, int N, typename W>
template <typename D>
bool EncodedBlocks<H, N, W>::readTreeBranch(TTree& tree, const std::string& brname, D& dt, int ev)
{
  auto* br = tree.GetBranch(brname.c_str());
  if (!br) {
    LOG(debug) << "Branch " << brname << " is absent";
    return false;
  }
  auto* ptr = &dt;
  br->SetAddress(&ptr);
  br->GetEntry(ev);
  br->ResetAddress();
  return true;
}

///_____________________________________________________________________________
/// add and fill single branch
template <typename H, int N, typename W>
template <typename D>
inline size_t EncodedBlocks<H, N, W>::fillTreeBranch(TTree& tree, const std::string& brname, D& dt, int compLevel, int splitLevel)
{
  auto* br = tree.GetBranch(brname.c_str());
  if (!br) {
    br = tree.Branch(brname.c_str(), &dt, 512, splitLevel);
    br->SetCompressionLevel(compLevel);
  }
  return br->Fill();
}

///_____________________________________________________________________________
/// Create its own flat copy in the destination empty flat object
template <typename H, int N, typename W>
void EncodedBlocks<H, N, W>::fillFlatCopy(EncodedBlocks& dest) const
{
  assert(dest.empty() && dest.mRegistry.getFreeSize() < estimateSize());
  dest.mANSHeader = mANSHeader;
  dest.mHeader = mHeader;
  dest.mMetadata = mMetadata;
  for (int i = 0; i < N; i++) {
    dest.mBlocks[i].store(mBlocks[i].getNDict(), mBlocks[i].getNData(), mBlocks[i].getDict(), mBlocks[i].getData());
  }
}

///_____________________________________________________________________________
/// Copy itself to flat buffer created on the fly from the vector
template <typename H, int N, typename W>
template <typename V>
void EncodedBlocks<H, N, W>::copyToFlat(V& vec)
{
  auto vtsz = sizeof(typename std::remove_reference<decltype(vec)>::type::value_type), sz = estimateSize();
  vec.resize(sz / vtsz);
  copyToFlat(vec.data());
}

///_____________________________________________________________________________
/// Estimate size of the buffer needed to store all compressed data in a contiguos block of memory, accounting for alignment
/// This method is to be called after reading object from the tree as a non-flat object!
template <typename H, int N, typename W>
size_t EncodedBlocks<H, N, W>::estimateSize() const
{
  size_t sz = 0;
  sz += alignSize(sizeof(*this));
  for (int i = 0; i < N; i++) {
    sz += alignSize(mBlocks[i].nStored * sizeof(W));
  }
  return sz;
}

///_____________________________________________________________________________
/// Estimate size from metadata
/// This method is to be called after reading object from the tree as a non-flat object!
template <typename H, int N, typename W>
size_t EncodedBlocks<H, N, W>::estimateSizeFromMetadata() const
{
  size_t sz = alignSize(sizeof(*this));
  for (int i = 0; i < N; i++) {
    sz += alignSize((mMetadata[i].nDictWords + mMetadata[i].nDataWords + mMetadata[i].nLiteralWords) * sizeof(W));
  }
  return sz;
}

///_____________________________________________________________________________
/// expand the storage to new size in bytes
template <typename H, int N, typename W>
template <typename buffer_T>
auto EncodedBlocks<H, N, W>::expand(buffer_T& buffer, size_t newsizeBytes)
{
  auto buftypesize = sizeof(typename std::remove_reference<decltype(buffer)>::type::value_type);
  auto* oldHead = get(buffer.data())->mRegistry.head;
  buffer.resize(alignSize(newsizeBytes) / buftypesize);
  relocate(oldHead, reinterpret_cast<char*>(buffer.data()), reinterpret_cast<char*>(buffer.data()), newsizeBytes);
  return get(buffer.data());
}

///_____________________________________________________________________________
/// relocate to different head position, newHead points on start of the dynamic buffer holding the data.
/// the address of the static part might be actually different (wrapper). This different newHead and
/// wrapper addresses must be used when the buffer pointed by newHead is const (e.g. received from the
/// DPL input), in this case we create a wrapper, which points on these const data
template <typename H, int N, typename W>
void EncodedBlocks<H, N, W>::relocate(const char* oldHead, char* newHead, char* wrapper, size_t newsize)
{
  auto newStr = get(wrapper);
  for (int i = 0; i < N; i++) {
    newStr->mBlocks[i].relocate(oldHead, newHead, wrapper);
  }
  newStr->mRegistry.head = newHead; // newHead points on the real data
  // if asked, update the size
  if (newsize) { // in bytes!!!
    assert(newStr->estimateSize() <= newsize);
    newStr->mRegistry.size = newsize;
  }
}

///_____________________________________________________________________________
/// setup internal structure and registry for given buffer size (in bytes!!!)
template <typename H, int N, typename W>
void EncodedBlocks<H, N, W>::init(size_t sz)
{
  mRegistry.head = reinterpret_cast<char*>(this);
  mRegistry.size = sz;
  mRegistry.offsFreeStart = alignSize(sizeof(*this));
  for (int i = 0; i < N; i++) {
    mMetadata[i].clear();
    mBlocks[i].registry = &mRegistry;
    mBlocks[i].clear();
  }
}

///_____________________________________________________________________________
/// clear itself
template <typename H, int N, typename W>
void EncodedBlocks<H, N, W>::clear()
{
  for (int i = 0; i < N; i++) {
    mBlocks[i].clear();
    mMetadata[i].clear();
  }
  mRegistry.offsFreeStart = alignSize(sizeof(*this));
}

///_____________________________________________________________________________
/// get const image of the container wrapper, with pointers in the image relocated to new head
template <typename H, int N, typename W>
const auto EncodedBlocks<H, N, W>::getImage(const void* newHead)
{
  auto image(*get(newHead)); // 1st make a shalow copy
  // now fix its pointers
  // we don't modify newHead, but still need to remove constness for relocation interface
  relocate(image.mRegistry.head, const_cast<char*>(reinterpret_cast<const char*>(newHead)), reinterpret_cast<char*>(&image));

  return std::move(image);
}

///_____________________________________________________________________________
/// create container from arbitrary buffer of predefined size (in bytes!!!). Head is supposed to respect the alignment
template <typename H, int N, typename W>
inline auto EncodedBlocks<H, N, W>::create(void* head, size_t sz)
{
  auto b = get(head);
  b->init(sz);
  return b;
}

///_____________________________________________________________________________
/// create container from arbitrary buffer of predefined size (in bytes!!!). Head is supposed to respect the alignment
template <typename H, int N, typename W>
template <typename VD>
inline auto EncodedBlocks<H, N, W>::create(VD& v)
{
  size_t vsz = sizeof(typename std::remove_reference<decltype(v)>::type::value_type); // size of the element of the buffer
  auto baseSize = getMinAlignedSize() / vsz;
  if (v.size() < baseSize) {
    v.resize(baseSize);
  }
  return create(v.data(), v.size() * vsz);
}

///_____________________________________________________________________________
/// print itself
template <typename H, int N, typename W>
void EncodedBlocks<H, N, W>::print(const std::string& prefix, int verbosity) const
{
  if (verbosity > 0) {
    LOG(info) << prefix << "Container of " << N << " blocks, size: " << size() << " bytes, unused: " << getFreeSize();
    for (int i = 0; i < N; i++) {
      LOG(info) << "Block " << i << " for " << mMetadata[i].messageLength << " message words of " << mMetadata[i].messageWordSize << " bytes |"
                << " NDictWords: " << mBlocks[i].getNDict() << " NDataWords: " << mBlocks[i].getNData()
                << " NLiteralWords: " << mBlocks[i].getNLiterals();
    }
  } else if (verbosity == 0) {
    size_t inpSize = 0, ndict = 0, ndata = 0, nlit = 0;
    for (int i = 0; i < N; i++) {
      inpSize += mMetadata[i].messageLength * mMetadata[i].messageWordSize;
      ndict += mBlocks[i].getNDict();
      ndata += mBlocks[i].getNData();
      nlit += mBlocks[i].getNLiterals();
    }
    LOG(info) << prefix << N << " blocks, input size: " << inpSize << ", output size: " << size()
              << " NDictWords: " << ndict << " NDataWords: " << ndata << " NLiteralWords: " << nlit;
  }
}

///_____________________________________________________________________________
template <typename H, int N, typename W>
template <class container_T, class container_IT>
inline void EncodedBlocks<H, N, W>::decode(container_T& dest,            // destination container
                                           int slot,                     // slot of the block to decode
                                           const void* decoderExt) const // optional externally provided decoder
{
  dest.resize(mMetadata[slot].messageLength); // allocate output buffer
  decode(std::begin(dest), slot, decoderExt);
}

///_____________________________________________________________________________
template <typename H, int N, typename W>
template <typename D_IT, std::enable_if_t<detail::is_iterator_v<D_IT>, bool>>
void EncodedBlocks<H, N, W>::decode(D_IT dest,                    // iterator to destination
                                    int slot,                     // slot of the block to decode
                                    const void* decoderExt) const // optional externally provided decoder
{
  // get references to the right data
  const auto& block = mBlocks[slot];
  const auto& md = mMetadata[slot];

  using dest_t = typename std::iterator_traits<D_IT>::value_type;

  // decode
  if (block.getNStored()) {
    if (md.opt == Metadata::OptStore::EENCODE) {
      if (!decoderExt && !block.getNDict()) {
        LOG(error) << "Dictionaty is not saved for slot " << slot << " and no external decoder is provided";
        throw std::runtime_error("Dictionary is not saved and no external decoder provided");
      }
      const o2::rans::LiteralDecoder64<dest_t>* decoder = reinterpret_cast<const o2::rans::LiteralDecoder64<dest_t>*>(decoderExt);
      std::unique_ptr<o2::rans::LiteralDecoder64<dest_t>> decoderLoc;
      if (block.getNDict()) { // if dictionaty is saved, prefer it
        o2::rans::FrequencyTable frequencies;
        frequencies.addFrequencies(block.getDict(), block.getDict() + block.getNDict(), md.min, md.max);
        decoderLoc = std::make_unique<o2::rans::LiteralDecoder64<dest_t>>(frequencies, md.probabilityBits);
        decoder = decoderLoc.get();
      } else { // verify that decoded corresponds to stored metadata
        if (md.min != decoder->getMinSymbol() || md.max != decoder->getMaxSymbol()) {
          LOG(error) << "Mismatch between min=" << md.min << "/" << md.max << " symbols in metadata and those in external decoder "
                     << decoder->getMinSymbol() << "/" << decoder->getMaxSymbol() << " for slot " << slot;
          throw std::runtime_error("Mismatch between min/max symbols in metadata and those in external decoder");
        }
      }
      // load incompressible symbols if they existed
      std::vector<dest_t> literals;
      if (block.getNLiterals()) {
        // note: here we have to use md.nLiterals (original number of literal words) rather than md.nLiteralWords == block.getNLiterals()
        // (number of W-words in the EncodedBlock occupied by literals) as we cast literals stored in W-word array
        // to D-word array
        literals = std::vector<dest_t>{reinterpret_cast<const dest_t*>(block.getLiterals()), reinterpret_cast<const dest_t*>(block.getLiterals()) + md.nLiterals};
      }
      decoder->process(block.getData() + block.getNData(), dest, md.messageLength, literals);
    } else { // data was stored as is
      using destPtr_t = typename std::iterator_traits<D_IT>::pointer;
      destPtr_t srcBegin = reinterpret_cast<destPtr_t>(block.payload);
      destPtr_t srcEnd = srcBegin + md.messageLength * sizeof(dest_t);
      std::copy(srcBegin, srcEnd, dest);
      //std::memcpy(dest, block.payload, md.messageLength * sizeof(dest_t));
    }
  }
}

///_____________________________________________________________________________
template <typename H, int N, typename W>
template <typename input_IT, typename buffer_T>
void EncodedBlocks<H, N, W>::encode(const input_IT srcBegin,      // iterator begin of source message
                                    const input_IT srcEnd,        // iterator end of source message
                                    int slot,                     // slot in encoded data to fill
                                    uint8_t symbolTablePrecision, // encoding into
                                    Metadata::OptStore opt,       // option for data compression
                                    buffer_T* buffer,             // optional buffer (vector) providing memory for encoded blocks
                                    const void* encoderExt,       // optional external encoder
                                    float memfc)                  // memory allocation margin factor
{

  using storageBuffer_t = W;
  using input_t = typename std::iterator_traits<input_IT>::value_type;
  using ransEncoder_t = typename rans::LiteralEncoder64<input_t>;
  using ransState_t = typename ransEncoder_t::coder_t;
  using ransStream_t = typename ransEncoder_t::stream_t;

  // assert at compile time that output types align so that padding is not necessary.
  static_assert(std::is_same_v<storageBuffer_t, ransStream_t>);
  static_assert(std::is_same_v<storageBuffer_t, typename rans::FrequencyTable::count_t>);

  // fill a new block
  assert(slot == mRegistry.nFilledBlocks);
  mRegistry.nFilledBlocks++;

  const size_t messageLength = std::distance(srcBegin, srcEnd);
  // cover three cases:
  // * empty source message: no entropy coding
  // * source message to pass through without any entropy coding
  // * source message where entropy coding should be applied

  // case 1: empty source message
  if (messageLength == 0) {
    mMetadata[slot] = Metadata{0, 0, sizeof(input_t), sizeof(ransState_t), sizeof(ransStream_t), symbolTablePrecision, Metadata::OptStore::NODATA, 0, 0, 0, 0, 0};
    return;
  }

  auto* thisBlock = &mBlocks[slot];
  auto* thisMetadata = &mMetadata[slot];

  // resize underlying buffer of block if necessary and update all pointers.
  auto expandStorage = [&](int additionalElements) {
    auto* const blockHead = get(thisBlock->registry->head);                         // extract pointer from the block, as "this" might be invalid
    const size_t additionalSize = blockHead->estimateBlockSize(additionalElements); // size in bytes!!!
    if (additionalSize >= thisBlock->registry->getFreeSize()) {
      LOG(debug) << "Slot " << slot << ": free size: " << thisBlock->registry->getFreeSize() << ", need " << additionalSize << " for " << additionalElements << " words";
      if (buffer) {
        blockHead->expand(*buffer, blockHead->size() + (additionalSize - blockHead->getFreeSize()));
        thisMetadata = &(get(buffer->data())->mMetadata[slot]);
        thisBlock = &(get(buffer->data())->mBlocks[slot]); // in case of resizing this and any this.xxx becomes invalid
      } else {
        throw std::runtime_error("no room for encoded block in provided container");
      }
    }
  };

  // case 3: message where entropy coding should be applied
  if (opt == Metadata::OptStore::EENCODE) {
    // build symbol statistics
    constexpr size_t SizeEstMarginAbs = 10 * 1024;
    const float SizeEstMarginRel = 1.5 * memfc;

    const auto [inplaceEncoder, frequencyTable] = [&]() {
      if (encoderExt) {
        return std::make_tuple(ransEncoder_t{}, rans::FrequencyTable{});
      } else {
        rans::FrequencyTable frequencyTable{};
        frequencyTable.addSamples(srcBegin, srcEnd);
        return std::make_tuple(ransEncoder_t{frequencyTable, symbolTablePrecision}, frequencyTable);
      }
    }();
    ransEncoder_t const* const encoder = encoderExt ? reinterpret_cast<ransEncoder_t const* const>(encoderExt) : &inplaceEncoder;

    // estimate size of encode buffer
    int dataSize = rans::calculateMaxBufferSize(messageLength, encoder->getAlphabetRangeBits(), sizeof(input_t)); // size in bytes
    // preliminary expansion of storage based on dict size + estimated size of encode buffer
    dataSize = SizeEstMarginAbs + int(SizeEstMarginRel * (dataSize / sizeof(storageBuffer_t))) + (sizeof(input_t) < sizeof(storageBuffer_t)); // size in words of output stream
    expandStorage(frequencyTable.size() + dataSize);
    //store dictionary first
    if (frequencyTable.size()) {
      thisBlock->storeDict(frequencyTable.size(), frequencyTable.data());
      LOGP(debug, "StoreDict {} bytes, offs: {}:{}", frequencyTable.size() * sizeof(W), thisBlock->getOffsDict(), thisBlock->getOffsDict() + frequencyTable.size() * sizeof(W));
    }
    // vector of incompressible literal symbols
    std::vector<input_t> literals;
    // directly encode source message into block buffer.
    storageBuffer_t* const blockBufferBegin = thisBlock->getCreateData();
    const size_t maxBufferSize = thisBlock->registry->getFreeSize(); // note: "this" might be not valid after expandStorage call!!!
    const auto encodedMessageEnd = encoder->process(srcBegin, srcEnd, blockBufferBegin, literals);
    rans::utils::checkBounds(encodedMessageEnd, blockBufferBegin + maxBufferSize / sizeof(W));
    dataSize = encodedMessageEnd - thisBlock->getDataPointer();
    thisBlock->setNData(dataSize);
    thisBlock->realignBlock();
    LOGP(debug, "StoreData {} bytes, offs: {}:{}", dataSize * sizeof(W), thisBlock->getOffsData(), thisBlock->getOffsData() + dataSize * sizeof(W));
    // update the size claimed by encode message directly inside the block

    // store incompressible symbols if any
    const size_t nLiteralSymbols = literals.size();
    const size_t nLiteralWords = [&]() {
      if (!literals.empty()) {
        const size_t nSymbols = literals.size();
        // introduce padding in case literals don't align;
        const size_t nLiteralSymbolsPadded = calculatePaddedSize<input_t, storageBuffer_t>(nSymbols);
        literals.resize(nLiteralSymbolsPadded, {});

        const size_t nLiteralStorageElems = calculateNDestTElements<input_t, storageBuffer_t>(nSymbols);
        expandStorage(nLiteralStorageElems);
        thisBlock->storeLiterals(nLiteralStorageElems, reinterpret_cast<const storageBuffer_t*>(literals.data()));
        LOGP(debug, "StoreLiterals {} bytes, offs: {}:{}", nLiteralStorageElems * sizeof(W), thisBlock->getOffsLiterals(), thisBlock->getOffsLiterals() + nLiteralStorageElems * sizeof(W));
        return nLiteralStorageElems;
      }
      return size_t(0);
    }();

    *thisMetadata = Metadata{messageLength,
                             nLiteralSymbols,
                             sizeof(input_t),
                             sizeof(ransState_t),
                             sizeof(ransStream_t),
                             static_cast<uint8_t>(encoder->getSymbolTablePrecision()),
                             opt,
                             encoder->getMinSymbol(),
                             encoder->getMaxSymbol(),
                             static_cast<int32_t>(frequencyTable.size()),
                             dataSize,
                             static_cast<int32_t>(nLiteralWords)};
  } else { // store original data w/o EEncoding
    //FIXME(milettri): we should be able to do without an intermediate vector;
    // provided iterator is not necessarily pointer, need to use intermediate vector!!!

    // introduce padding in case literals don't align;
    const size_t nSourceElemsPadded = calculatePaddedSize<input_t, storageBuffer_t>(messageLength);
    std::vector<input_t> tmp(nSourceElemsPadded, {});
    std::copy(srcBegin, srcEnd, std::begin(tmp));

    const size_t nBufferElems = calculateNDestTElements<input_t, storageBuffer_t>(messageLength);
    expandStorage(nBufferElems);
    thisBlock->storeData(thisMetadata->nDataWords, reinterpret_cast<const storageBuffer_t*>(tmp.data()));

    *thisMetadata = Metadata{messageLength, 0, sizeof(input_t), sizeof(ransState_t), sizeof(storageBuffer_t), symbolTablePrecision, opt, 0, 0, 0, static_cast<int>(nBufferElems), 0};
  }
}

/// create a special EncodedBlocks containing only dictionaries made from provided vector of frequency tables
template <typename H, int N, typename W>
std::vector<char> EncodedBlocks<H, N, W>::createDictionaryBlocks(const std::vector<o2::rans::FrequencyTable>& vfreq, const std::vector<Metadata>& vmd)
{
  if (vfreq.size() != N) {
    throw std::runtime_error(fmt::format("mismatch between the size of frequencies vector {} and number of blocks {}", vfreq.size(), N));
  }
  size_t sz = alignSize(sizeof(EncodedBlocks<H, N, W>));
  for (int ib = 0; ib < N; ib++) {
    sz += Block<W>::estimateSize(vfreq[ib].size());
  }
  std::vector<char> vdict(sz); // memory space for dictionary
  auto dictBlocks = create(vdict.data(), sz);
  for (int ib = 0; ib < N; ib++) {
    if (vfreq[ib].size()) {
      LOG(info) << "adding dictionary of " << vfreq[ib].size() << " words for block " << ib << ", min/max= " << vfreq[ib].getMinSymbol() << "/" << vfreq[ib].getMaxSymbol();
      dictBlocks->mBlocks[ib].storeDict(vfreq[ib].size(), vfreq[ib].data());
      dictBlocks = get(vdict.data()); // !!! rellocation might have invalidated dictBlocks pointer
      dictBlocks->mMetadata[ib] = vmd[ib];
      dictBlocks->mMetadata[ib].opt = Metadata::OptStore::ROOTCompression; // we will compress the dictionary with root!
      dictBlocks->mBlocks[ib].realignBlock();
    } else {
      dictBlocks->mMetadata[ib].opt = Metadata::OptStore::NONE;
    }
    dictBlocks->mRegistry.nFilledBlocks++;
  }
  return std::move(vdict);
}

template <typename H, int N, typename W>
void EncodedBlocks<H, N, W>::dump(const std::string& prefix, int ncol) const
{
  for (int ibl = 0; ibl < getNBlocks(); ibl++) {
    const auto& blc = getBlock(ibl);
    std::string ss;
    LOGP(info, "{} Bloc:{} Dict: {} words", prefix, ibl, blc.getNDict());
    const auto* ptr = blc.getDict();
    for (int i = 0; i < blc.getNDict(); i++) {
      if (i && (i % ncol) == 0) {
        LOG(info) << ss;
        ss.clear();
      }
      ss += fmt::format(" {:#010x}", ptr[i]);
    }
    if (!ss.empty()) {
      LOG(info) << ss;
      ss.clear();
    }
    LOG(info) << "\n";
    LOGP(info, "{} Bloc:{} Data: {} words", prefix, ibl, blc.getNData());
    ptr = blc.getData();
    for (int i = 0; i < blc.getNData(); i++) {
      if (i && (i % ncol) == 0) {
        LOG(info) << ss;
        ss.clear();
      }
      ss += fmt::format(" {:#010x}", ptr[i]);
    }
    if (!ss.empty()) {
      LOG(info) << ss;
      ss.clear();
    }
    LOG(info) << "\n";
    LOGP(info, "{} Bloc:{} Literals: {} words", prefix, ibl, blc.getNLiterals());
    ptr = blc.getData();
    for (int i = 0; i < blc.getNLiterals(); i++) {
      if (i && (i % 20) == 0) {
        LOG(info) << ss;
        ss.clear();
      }
      ss += fmt::format(" {:#010x}", ptr[i]);
    }
    if (!ss.empty()) {
      LOG(info) << ss;
      ss.clear();
    }
    LOG(info) << "\n";
  }
}

} // namespace ctf
} // namespace o2

#endif
