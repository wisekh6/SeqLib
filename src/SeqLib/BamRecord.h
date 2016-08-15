#ifndef SEQLIB_BAM_RECORD_H__
#define SEQLIB_BAM_RECORD_H__

#include <cstdint>
#include <vector>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <cassert>
#include <algorithm>

#include "htslib/hts.h"
#include "htslib/sam.h"
#include "htslib/bgzf.h"
#include "htslib/kstring.h"
#include "htslib/faidx.h"

#include "SeqLib/GenomicRegion.h"

static const char BASES[16] = {' ', 'A', 'C', ' ',
                               'G', ' ', ' ', ' ', 
                               'T', ' ', ' ', ' ', 
                               ' ', ' ', ' ', 'N'};

static std::string cigar_delimiters = "MIDNSHPX";

static const uint8_t CIGTAB[255] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
#define FRORIENTATION 0
#define FFORIENTATION 1
#define RFORIENTATION 2
#define RRORIENTATION 3
#define UDORIENTATION 4

namespace SeqLib {

enum class Base { A = 1, C = 2, G = 4, T = 8, N = 15 };

/** Basic container for a single cigar operation
 *
 * Stores a single cigar element in a compact 32bit form (same as HTSlib).
 */
class CigarField {

 public:

  /** Construct the cigar op by type (MIDNSHPX) and length */
  CigarField(char t, uint32_t l); 

  /** Construct the cigar op from the raw sam.h uint32_t (first 4 bits op, last 28 len) */
  CigarField(uint32_t f) : data(f) {}

  /** Return the raw sam.h uint8_t cigar data */
  inline uint32_t raw() const { return data; }

  /** Print the cigar field (eg 35M) */
  friend std::ostream& operator<<(std::ostream& out, const CigarField& c);

  /** Return the cigar op type (one of MIDNSHPX) as a char */
  inline char Type() const { return bam_cigar_opchr(data); } 

  /** Return the raw sam.h uint8_t cigar type (bam_cigar_op(data)) */
  inline uint8_t RawType() const { return bam_cigar_op(data); } 

  /** Return the length of the cigar op (eg 35M returns 35) */
  inline uint32_t Length() const { return bam_cigar_oplen(data); } 

  /** Returns true if cigar op matches bases on the reference (MDN=X) */
  inline bool ConsumesReference() const { return bam_cigar_type(bam_cigar_op(data))&2;  }

  /** Returuns true cigar op matches bases on the query (MIS=X) */
  inline bool ConsumesQuery() const { return bam_cigar_type(bam_cigar_op(data))&1;  }

 private:

  // first 4 bits hold op, last 28 hold len
  uint32_t data;
  
};

/** CIGAR for a single gapped alignment
 *
 * Constructed as a vector of CigarField objects. 
 */
 class Cigar {
   
 public:

   /** Iterator to first cigar op */
   typename std::vector<CigarField>::iterator begin() { return m_data.begin(); } 

   /** Iterator to last cigar op */
   typename std::vector<CigarField>::iterator end() { return m_data.end(); }

   /** Const iterator to end of cigar op */
   typename std::vector<CigarField>::const_iterator begin() const { return m_data.begin(); } 

   /** Const iterator to end of cigar op */
   typename std::vector<CigarField>::const_iterator end() const { return m_data.end(); }

   /** Const reference to last cigar op */
   inline const CigarField& back() const { return m_data.back(); }

   /** Reference to last cigar op */
   inline CigarField& back() { return m_data.back(); }

   /** Const reference to first cigar op */
   inline const CigarField& front() const { return m_data.front(); }

   /** Reference to first cigar op */
   inline CigarField& front() { return m_data.front(); }

   /** Returns the number of cigar ops */
   size_t size() const { return m_data.size(); }

   /** Returns the i'th cigar op */
   CigarField& operator[](size_t i) { return m_data[i]; }

   /** Returns the i'th cigar op (const) */
   const CigarField& operator[](size_t i) const { return m_data[i]; }

   /** Add a new cigar op */
   void add(const CigarField& c) { 
     m_data.push_back(c); 
   }

  /** Print cigar string (eg 35M25S) */
  friend std::ostream& operator<<(std::ostream& out, const Cigar& c);
  
   
 private:
   
   std::vector<CigarField> m_data; // should make this simpler

 };

 //typedef std::vector<CigarField> Cigar;
typedef std::unordered_map<std::string, size_t> CigarMap;

 Cigar cigarFromString(const std::string& cig);

/** Class to store and interact with an HTSLib bam1_t read.
 *
 * HTSLibrary reads are stored in the bam1_t struct. Memory allocation
 * is taken care of by bam1_t init, and deallocation by destroy_bam1. This
 * class is a C++ interface that automatically takes care of memory management
 * for these C allocs/deallocs. The only member of BamRecord is a bam1_t object.
 * Alloc/dealloc is taken care of by the constructor and destructor.
 */
class BamRecord {

  friend class BLATWraper;
  friend class BWAWrapper;

 public:

  /** Construct a BamRecord with perfect "alignment"
   */
  BamRecord(const std::string& name, const std::string& seq, const GenomicRegion * gr, const Cigar& cig);
  
  /** Construct an empty BamRecord by calling bam_init1() 
   */
  void init();

  /** Check if a read is empty (not initialized)
   * @value true if read was not initialized with any values
   */
  bool isEmpty() const { return !b; }

  /** Explicitly pass a bam1_t to the BamRecord. 
   *
   * The BamRecord now controls the memory, and will delete at destruction
   * @param a An allocated bam1_t
   */
  void assign(bam1_t* a);

  /** Make a BamRecord with no memory allocated and a null header */
  BamRecord() {}

  /** BamRecord is aligned on reverse strand */
  inline bool ReverseFlag() const { return b ? ((b->core.flag&BAM_FREVERSE) != 0) : false; }

  /** BamRecord has mate aligned on reverse strand */
  inline bool MateReverseFlag() const { return b ? ((b->core.flag&BAM_FMREVERSE) != 0) : false; }

  /** BamRecord has is an interchromosomal alignment */
  inline bool Interchromosomal() const { return b ? b->core.tid != b->core.mtid && PairMappedFlag() : false; }

  /** BamRecord is a duplicate */
  inline bool DuplicateFlag() const { return b ? ((b->core.flag&BAM_FDUP) != 0) : false; }

  /** BamRecord is a secondary alignment */
  inline bool SecondaryFlag() const { return b ? ((b->core.flag&BAM_FSECONDARY) != 0) : false; }

  /** BamRecord is paired */
  inline bool PairedFlag() const { return b ? ((b->core.flag&BAM_FPAIRED) != 0) : false; }

  /** Get the relative pair orientations 
   * 
   * 0 - FR (RFORIENTATION) (lower pos read is Fwd strand, higher is reverse)
   * 1 - FF (FFORIENTATION)
   * 2 - RF (RFORIENTATION)
   * 3 - RR (RRORIENTATION)
   * 4 - Undefined (UDORIENTATION) (unpaired or one/both is unmapped)
   */
  inline int PairOrientation() const {
    if (!PairMappedFlag())
      return UDORIENTATION;
    else if ( (!ReverseFlag() && Position() <= MatePosition() &&  MateReverseFlag() ) || // read 1
	      (ReverseFlag()  && Position() >= MatePosition() && !MateReverseFlag() ) ) // read 2
      return FRORIENTATION;
    else if (!ReverseFlag() && !MateReverseFlag())
      return FFORIENTATION;
    else if (ReverseFlag() && MateReverseFlag())
      return RRORIENTATION;
    else if (   ( ReverseFlag() && Position() < MatePosition() && !MateReverseFlag()) ||
                (!ReverseFlag() && Position() > MatePosition() &&  MateReverseFlag()))
      return RFORIENTATION;
    assert(false);
  }
  

  /** BamRecord is failed QC */
  inline bool QCFailFlag() const { return b ? ((b->core.flag&BAM_FQCFAIL) != 0) : false; }

  /** BamRecord is mapped */
  inline bool MappedFlag() const { return b ? ((b->core.flag&BAM_FUNMAP) == 0) : false; }

  /** BamRecord mate is mapped */
  inline bool MateMappedFlag() const { return b ? ((b->core.flag&BAM_FMUNMAP) == 0) : false; }

  /** BamRecord is mapped and mate is mapped and in pair */
  inline bool PairMappedFlag() const { return b ? (!(b->core.flag&BAM_FMUNMAP) && !(b->core.flag&BAM_FUNMAP) && (b->core.flag&BAM_FPAIRED) ) : false; }

  /** BamRecord is mapped in proper pair */
  inline bool ProperPair() const { return b ? (b->core.flag&BAM_FPROPER_PAIR) : false;} 

  /** BamRecord has proper orientation (FR) */
  inline bool ProperOrientation() const { 
    if (!b)
      return false;
    
    // mate on diff chrom gets false
    if (b->core.tid != b->core.mtid)
      return false;

    // if FR return true
    if (b->core.pos < b->core.mpos) {
      return (b->core.flag&BAM_FREVERSE) == 0 && (b->core.flag&BAM_FMREVERSE) != 0 ? true : false;
    } else {
      return (b->core.flag&BAM_FREVERSE) == 0 && (b->core.flag&BAM_FMREVERSE) != 0 ? false : true;
    }
      
  }

  /** Count the total number of N bases in this sequence */
  int32_t CountNBases() const;

  /** Trim the sequence down by removing bases from ends with low quality scores */
  void QualityTrimmedSequence(int32_t qualTrim, int32_t& startpoint, int32_t& endpoint) const;

  /** Retrieve the quality trimmed seqeuence from QT tag if made. Otherwise return normal seq */
  std::string QualitySequence() const;

  /** Get the alignment position */
  inline int32_t Position() const { return b ? b->core.pos : -1; }
  
  /** Get the alignment position of mate */
  inline int32_t MatePosition() const { return b ? b->core.mpos: -1; }

  /** Count the number of secondary alignments by looking at XA and XP tags */
  int32_t CountSecondaryAlignments() const;

  /** Get the end of the alignment */
  inline int32_t PositionEnd() const { return b ? bam_endpos(b.get()) : -1; }

  /** Get the chromosome ID of the read */
  inline int32_t ChrID() const { return b ? b->core.tid : -1; }
  
  /** Get the chrosome ID of the mate read */
  inline int32_t MateChrID() const { return b ? b->core.mtid : -1; }
  
  /** Get the mapping quality */
  inline int32_t MapQuality() const { return b ? b->core.qual : -1; }

  /** Set the mapping quality */
  inline void SetMapQuality(int32_t m) { b->core.qual = m; }
  
  /** Get the number of cigar fields */
  inline int32_t CigarSize() const { return b ? b->core.n_cigar : -1; }
  
  /** Check if this read is first in pair */
  inline bool FirstFlag() const { return (b->core.flag&BAM_FREAD1); }
  
  /** Get the qname of this read as a string */
  inline std::string Qname() const { return std::string(bam_get_qname(b)); }
  
  /** Get the qname of this read as a char array */
  inline char* QnameChar() const { return bam_get_qname(b); }
  
  /** Get the full alignment flag for this read */
  inline uint32_t AlignmentFlag() const { return b->core.flag; }
  
  /** Get the insert size for this read */
  inline int32_t InsertSize() const { return b->core.isize; } 

  /** Get the read group, first from qname, then by RG tag 
   * @return empty string if no readgroup found
   */
  inline std::string ParseReadGroup() const {

    // try to get from RG tag first
    std::string RG = GetZTag("RG");
    if (!RG.empty())
      return RG;

    // try to get the read group tag from qname second
    std::string qn = Qname();
    size_t posr = qn.find(":", 0);
    return (posr != std::string::npos) ? qn.substr(0, posr) : "NA";
  }

  /** Get the insert size, absolute value, and always taking into account read length */
  inline int32_t FullInsertSize() const {

    if (b->core.tid != b->core.mtid || !PairMappedFlag())
      return 0;

    return std::abs(b->core.pos - b->core.mpos) + Length();

  }
  
  /** Get the number of query bases of this read (aka length) */
  inline int32_t Length() const { return b->core.l_qseq; }
  
  /** Append a tag with new value, delimited by 'x' */
  void SmartAddTag(const std::string& tag, const std::string& val);
  
  /** Set the query name */
  void SetQname(const std::string& n);

  /** Set the sequence name */
  void SetSequence(const std::string& seq);

  /** Print a SAM-lite record for this alignment */
  friend std::ostream& operator<<(std::ostream& out, const BamRecord &r);

  /** Return read as a GenomicRegion */
  GenomicRegion asGenomicRegion() const;

  /** Return mate read as a GenomicRegion */
  GenomicRegion asGenomicRegionMate() const;

  /** Return the max single insertion size on this cigar */
  inline uint32_t MaxInsertionBases() const {
    uint32_t* c = bam_get_cigar(b);
    uint32_t imax = 0;
    for (size_t i = 0; i < b->core.n_cigar; i++) 
      if (bam_cigar_opchr(c[i]) == 'I')
	imax = std::max(bam_cigar_oplen(c[i]), imax);
    return imax;
  }

  /** Return the max single deletion size on this cigar */
  inline uint32_t MaxDeletionBases() const {
    uint32_t* c = bam_get_cigar(b);
    uint32_t dmax = 0;
    for (size_t i = 0; i < b->core.n_cigar; i++) 
      if (bam_cigar_opchr(c[i]) == 'D')
	dmax = std::max(bam_cigar_oplen(c[i]), dmax);
    return dmax;
  }

  /** Get the number of matched bases in this alignment */
  inline uint32_t NumMatchBases() const {
    uint32_t* c = bam_get_cigar(b);
    uint32_t dmax = 0;
    for (size_t i = 0; i < b->core.n_cigar; i++) 
      if (bam_cigar_opchr(c[i]) == 'M')
	dmax += bam_cigar_oplen(c[i]);
    return dmax;
  }


  /** Retrieve the CIGAR as a more managable Cigar structure */
  Cigar GetCigar() const {
    uint32_t* c = bam_get_cigar(b);
    Cigar cig;
    for (int k = 0; k < b->core.n_cigar; ++k) 
      cig.add(CigarField(c[k]));
    return cig;
  }

  /** Get the length of the alignment (regardless of hardclipping) */
  int32_t AlignmentLength() const;

  /** Retrieve the inverse of the CIGAR as a more managable Cigar structure */
  Cigar GetReverseCigar() const {
    uint32_t* c = bam_get_cigar(b);
    Cigar cig;
    for (int k = b->core.n_cigar - 1; k >= 0; --k) 
      //cig.add(CigarField(c[k]));
      cig.add(CigarField(c[k]));
      //cig.push_back(CigarField(c[k]));
    //cig.add(CigarField("MIDSSHP=XB"[c[k]&BAM_CIGAR_MASK], bam_cigar_oplen(c[k])));
    return cig;
  }

  /** Remove the sequence, quality and alignment tags */
  void clearSeqQualAndTags();

  /** Get the sequence of this read as a string */
  /*inline */std::string Sequence() const;

  /** Return the mean phred score 
   */
  double MeanPhred() const;

  /** Do a smith waterman alignment
   */
  BamRecord(const std::string& name, const std::string& seq, const std::string& ref, const GenomicRegion * gr);

  /** Get the quality scores of this read as a string */
  inline std::string Qualities() const { 
    uint8_t * p = bam_get_qual(b);
    std::string out(b->core.l_qseq, ' ');
    for (int32_t i = 0; i < b->core.l_qseq; ++i) 
      out[i] = (char)(p[i]+33);
    return out;
  }

  /** Get the start of the alignment on the read, by removing soft-clips
   * Do this in the reverse orientation though.
   */
  inline int32_t AlignmentPositionReverse() const {
    uint32_t* c = bam_get_cigar(b);
    int32_t p = 0;
    for (int32_t i = b->core.n_cigar - 1; i >= 0; --i) {
      if ( (bam_cigar_opchr(c[i]) == 'S') || (bam_cigar_opchr(c[i]) == 'H'))
	p += bam_cigar_oplen(c[i]);
      else // not a clip, so stop counting
	break;
    }
    return p;
  }
  
  /** Get the end of the alignment on the read, by removing soft-clips
   * Do this in the reverse orientation though.
   */
  inline int32_t AlignmentEndPositionReverse() const {
    uint32_t* c = bam_get_cigar(b);
    int32_t p = 0;
    for (int32_t i = 0; i < b->core.n_cigar; ++i) { // loop from the end
      if ( (bam_cigar_opchr(c[i]) == 'S') || (bam_cigar_opchr(c[i]) == 'H'))
	p += bam_cigar_oplen(c[i]);
      else // not a clip, so stop counting
	break;
    }
    return (b->core.l_qseq - p);
  }


  /** Get the start of the alignment on the read, by removing soft-clips
   */
  inline int32_t AlignmentPosition() const {
    uint32_t* c = bam_get_cigar(b);
    int32_t p = 0;
    for (int32_t i = 0; i < b->core.n_cigar; ++i) {
      if ( (bam_cigar_opchr(c[i]) == 'S') || (bam_cigar_opchr(c[i]) == 'H'))
	p += bam_cigar_oplen(c[i]);
      else // not a clip, so stop counting
	break;
    }
    return p;
  }
  
  /** Get the end of the alignment on the read, by removing soft-clips
   */
  inline int32_t AlignmentEndPosition() const {
    uint32_t* c = bam_get_cigar(b);
    int32_t p = 0;
    for (int32_t i = b->core.n_cigar - 1; i >= 0; --i) { // loop from the end
      if ( (bam_cigar_opchr(c[i]) == 'S') || (bam_cigar_opchr(c[i]) == 'H'))
	p += bam_cigar_oplen(c[i]);
      else // not a clip, so stop counting
	break;
    }
    return (b->core.l_qseq - p);
  }

  /** Get the number of soft clipped bases */
  inline int32_t NumSoftClip() const {
      int32_t p = 0;
      uint32_t* c = bam_get_cigar(b);
      for (int32_t i = 0; i < b->core.n_cigar; ++i)
	if (bam_cigar_opchr(c[i]) == 'S')
	  p += bam_cigar_oplen(c[i]);
      return p;
    }

  /** Get the number of hard clipped bases */
  inline int32_t NumHardClip() const {
      int32_t p = 0;
      uint32_t* c = bam_get_cigar(b);
      for (int32_t i = 0; i < b->core.n_cigar; ++i) 
	if (bam_cigar_opchr(c[i]) == 'H')
	  p += bam_cigar_oplen(c[i]);
      return p;
    }


  /** Get the number of clipped bases (hard clipped and soft clipped) */
  inline int32_t NumClip() const {
    int32_t p = 0;
    uint32_t* c = bam_get_cigar(b);
    for (int32_t i = 0; i < b->core.n_cigar; ++i)
      if ( (bam_cigar_opchr(c[i]) == 'S') || (bam_cigar_opchr(c[i]) == 'H') )
	p += bam_cigar_oplen(c[i]);
    return p;
  }
  
  /** Get a string (Z) tag 
   * @param tag Name of the tag. eg "XP"
   * @return The value stored in the tag. Returns empty string if it does not exist.
   */
  std::string GetZTag(const std::string& tag) const;
  
  /** Get a vector of ints from a Z tag delimited by "x"
   * @param tag Name of the tag eg "AL"
   * @return A vector of ints, retrieved from the x delimited Z tag
   */
  std::vector<int> GetSmartIntTag(const std::string& tag) const;

  /** Get a vector of strings from a Z tag delimited by "x"
   * @param tag Name of the tag eg "CN"
   * @return A vector of strngs, retrieved from the x delimited Z tag
   */
  std::vector<std::string> GetSmartStringTag(const std::string& tag) const;

  /** Get an int (i) tag 
   * @param tag Name of the tag. eg "XP"
   * @return The value stored in the tag. Returns 0 if it does not exist.
   */
  inline int32_t GetIntTag(const std::string& tag) const {
    uint8_t* p = bam_aux_get(b.get(),tag.c_str());
    if (!p)
      return 0;
    return bam_aux2i(p);
  }


  /** Add a string (Z) tag
   * @param tag Name of the tag. eg "XP"
   * @param val Value for the tag
   */
  void AddZTag(std::string tag, std::string val);

  /** Add an int (i) tag
   * @param tag Name of the tag. eg "XP"
   * @param val Value for the tag
   */
  inline void AddIntTag(const std::string& tag, int32_t val) {
    bam_aux_append(b.get(), tag.data(), 'i', 4, (uint8_t*)&val);
  }

  /** Set the chr id number 
   * @param id Chromosome id. Typically is 0 for chr1, etc
   */
  inline void SetID(int32_t id) {
    b->core.tid = id;
  }
  
  /** Set the alignment start position
   * @param pos Alignment start position
   */
  inline void SetPosition(int32_t pos) {
    b->core.pos = pos;
  }

  /** Convert CIGAR to a string
   */
  inline std::string CigarString() const {
    std::stringstream cig;
    uint32_t* c = bam_get_cigar(b);
    for (int k = 0; k < b->core.n_cigar; ++k)
      cig << bam_cigar_oplen(c[k]) << "MIDNSHP=XB"[c[k]&BAM_CIGAR_MASK];
    return cig.str();
  }
  
  /** Retrieve the human readable chromosome name. 
   * 
   * Note that this requires that the header not be empty. If
   * it is empty, assumes this ia chr1 based reference
   * @note This will be deprecated
   */
  inline std::string ChrName(bam_hdr_t * h) const {

    // if we have the header, convert
    if (h) {
      if (b->core.tid < h->n_targets)
	return std::string(h->target_name[b->core.tid]);
      else
	return "CHR_ERROR";
    }

    // no header, assume zero based
    return std::to_string(b->core.tid + 1);
    
  }

  /** Return a human readable chromosome name assuming chr is indexed
   * from 0 (eg id 0 return "1")
   */
  inline std::string ChrName() const {
    return std::to_string(b->core.tid + 1);
  }

  /** Retrieve the human readable chromosome name. 
   * 
   * Note that this requires that the header not be empty. If
   * it is empty, assumes this ia chr1 based reference
   * @exception Throws an out_of_range exception if chr id is not in dictionary
   * @return Empty string if chr id < 0, otherwise chromosome name from dictionary.
   */
  inline std::string ChrName(const SeqLib::BamHeader& h) const {

    if (b->core.tid < 0)
      return std::string();
    
    return h.IDtoName(b->core.tid);
    // no header, assume zero based
    return std::to_string(b->core.tid + 1);
    
  }

  /** Return a short description (chr:pos) of this read */
  inline std::string Brief(bam_hdr_t * h = nullptr) const {
    if (!h)
      return(std::to_string(b->core.tid + 1) + ":" + AddCommas<int32_t>(b->core.pos) + "(" + ((b->core.flag&BAM_FREVERSE) != 0 ? "+" : "-") + ")");
    else
      return(std::string(h->target_name[b->core.tid]) + ":" + AddCommas<int32_t>(b->core.pos) + "(" + ((b->core.flag&BAM_FREVERSE) != 0 ? "+" : "-") + ")");      
  }

  /** Return a short description (chr:pos) of this read's mate */
  inline std::string BriefMate(bam_hdr_t * h = nullptr) const {
    if (!h)
      return(std::to_string(b->core.mtid + 1) + ":" + AddCommas<int32_t>(b->core.mpos) + "(" + ((b->core.flag&BAM_FMREVERSE) != 0 ? "+" : "-") + ")");
    else
      return(std::string(h->target_name[b->core.mtid]) + ":" + AddCommas<int32_t>(b->core.mpos) + "(" + ((b->core.flag&BAM_FMREVERSE) != 0 ? "+" : "-") + ")");      
  }

  /** Strip a particular alignment tag 
   * @param tag Tag to remove
   */
  inline void RemoveTag(const char* tag) {
    uint8_t* p = bam_aux_get(b.get(), tag);
    if (p)
      bam_aux_del(b.get(), p);
  }

  /** Strip all of the alignment tags */
  inline void RemoveAllTags() {
    size_t keep = (b->core.n_cigar<<2) + b->core.l_qname + ((b->core.l_qseq + 1)>>1) + b->core.l_qseq;
    b->data = (uint8_t*)realloc(b->data, keep); // free the end, which has aux data
    b->l_data = keep;
    b->m_data = b->l_data;
  }

  /** Return the raw pointer */
  inline bam1_t* raw() const { return b.get(); }

  /** Check if base at position on read is covered by alignment M or I (not clip)
   *
   * Example: Alignment with 50M10I10M20S -- 
   * 0-79: true, 80+ false
   * @param pos Position on base (0 is start)
   * @return true if that base is aligned (I or M)
   */
  bool coveredBase(int pos) const;

  /** Check if base at position on read is covered by match only (M)
   *
   * Example: Alignment with 10S50M20S -- 
   * 0-9: false, 10-59: true, 60+: false
   * @param pos Position on base (0 is start)
   * @return true if that base is aligned (M)
   */
  bool coveredMatchBase(int pos) const;

  std::shared_ptr<bam1_t> b; // need to move this to private  
  private:

};

 typedef std::vector<BamRecord> BamRecordVector; 
 
 typedef std::vector<BamRecordVector> BamRecordClusterVector;

 /** @brief Sort methods for reads
  */
 namespace BamRecordSort {

   /** @brief Sort by read position 
    */
   struct ByReadPosition
   {
     bool operator()( const BamRecord& lx, const BamRecord& rx ) const {
       return (lx.ChrID() < rx.ChrID()) || (lx.ChrID() == rx.ChrID() && lx.Position() < rx.Position());
     }
   };

   /** @brief Sort by read-mate position 
    */
   struct ByMatePosition
   {
     bool operator()( const BamRecord& lx, const BamRecord& rx ) const {
       return (lx.MateChrID() < rx.MateChrID()) || (lx.MateChrID() == rx.MateChrID() && lx.MatePosition() < rx.MatePosition());
     }
   };

}

}
#endif
