/*
============================================================================
DELLY: Structural variant discovery by integrated PE mapping and SR analysis
============================================================================
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
============================================================================
Contact: Tobias Rausch (rausch@embl.de)
============================================================================
*/

#ifndef CLUSTER_H
#define CLUSTER_H

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/math/distributions/binomial.hpp>

#include <htslib/sam.h>

#include "util.h"
#include "junction.h"

namespace torali
{


  // Reduced bam alignment record data structure
  struct BamAlignRecord {
    int32_t tid;         
    int32_t pos;
    int32_t mtid; 
    int32_t mpos;
    int32_t alen;
    int32_t malen;
    int32_t Median;
    int32_t Mad;
    int32_t maxNormalISize;
    uint32_t flag;
    uint8_t MapQuality;
  
    BamAlignRecord(bam1_t* rec, uint8_t pairQuality, uint16_t a, uint16_t ma, int32_t median, int32_t mad, int32_t maxISize) : tid(rec->core.tid), pos(rec->core.pos), mtid(rec->core.mtid), mpos(rec->core.mpos), alen(a), malen(ma), Median(median), Mad(mad), maxNormalISize(maxISize), flag(rec->core.flag), MapQuality(pairQuality) {}
  };

  // Sort reduced bam alignment records
  template<typename TRecord>
  struct SortBamRecords : public std::binary_function<TRecord, TRecord, bool>
  {
    inline bool operator()(TRecord const& s1, TRecord const& s2) const {
      if (s1.tid==s1.mtid) {
	return ((std::min(s1.pos, s1.mpos) < std::min(s2.pos, s2.mpos)) || 
		((std::min(s1.pos, s1.mpos) == std::min(s2.pos, s2.mpos)) && (std::max(s1.pos, s1.mpos) < std::max(s2.pos, s2.mpos))) ||
		((std::min(s1.pos, s1.mpos) == std::min(s2.pos, s2.mpos)) && (std::max(s1.pos, s1.mpos) == std::max(s2.pos, s2.mpos)) && (s1.maxNormalISize < s2.maxNormalISize)));
      } else {
	return ((s1.pos < s2.pos) ||
		((s1.pos == s2.pos) && (s1.mpos < s2.mpos)) ||
		((s1.pos == s2.pos) && (s1.mpos == s2.mpos) && (s1.maxNormalISize < s2.maxNormalISize)));
      }
    }
  };
  

  // Edge struct
  template<typename TWeight, typename TVertex>
  struct EdgeRecord {
    TVertex source;
    TVertex target;
    TWeight weight;
    
    EdgeRecord(TVertex s, TVertex t, TWeight w) : source(s), target(t), weight(w) {}
  };

  // Sort edge records
  template<typename TRecord>
  struct SortEdgeRecords : public std::binary_function<TRecord, TRecord, bool>
  {
    inline bool operator()(TRecord const& e1, TRecord const& e2) const {
      return ((e1.weight < e2.weight) || ((e1.weight == e2.weight) && (e1.source < e2.source)) || ((e1.weight == e2.weight) && (e1.source == e2.source) && (e1.target < e2.target)));
    }
  };




  
  // Edge struct
  template<typename TWeight, typename TVertex>
  struct SREdgeRecord {
    typedef TVertex TVertexType;
    TVertex source;
    TVertex target;
    TWeight weight;

    SREdgeRecord(TVertex s, TVertex t, TWeight w) : source(s), target(t), weight(w) {}
  };

  // Sort edge records
  template<typename TRecord>
  struct SortSREdgeRecords : public std::binary_function<TRecord, TRecord, bool>
  {
    inline bool operator()(TRecord const& e1, TRecord const& e2) const {
      return ((e1.weight < e2.weight) || ((e1.weight == e2.weight) && (e1.source < e2.source)) || ((e1.weight == e2.weight) && (e1.source == e2.source) && (e1.target < e2.target)));
    }
  };


  // Initialize clique, deletions
  template<typename TBamRecord, typename TSize>
  inline void
  _initClique(TBamRecord const& el, TSize& svStart, TSize& svEnd, TSize& wiggle, int32_t const svt) {
    if (_translocation(svt)) {
      uint8_t ct = _getSpanOrientation(svt);
      if (ct%2==0) {
	svStart = el.pos + el.alen;
	if (ct>=2) svEnd = el.mpos;
	else svEnd = el.mpos + el.malen;
      } else {
	svStart = el.pos;
	if (ct>=2) svEnd = el.mpos + el.malen;
	else svEnd = el.mpos;
      }
      wiggle=el.maxNormalISize;
    } else {
      if (svt == 0) {
	svStart = el.mpos + el.malen;
	svEnd = el.pos + el.alen;
	wiggle = el.maxNormalISize - std::max(el.alen, el.malen);
      } else if (svt == 1) {
	svStart = el.mpos;
	svEnd = el.pos;
	wiggle = el.maxNormalISize - std::max(el.alen, el.malen);
      } else if (svt == 2) {
	svStart = el.mpos + el.malen;
	svEnd = el.pos;
	wiggle =  -el.maxNormalISize;
      } else if (svt == 3) {
	svStart = el.mpos;
	svEnd = el.pos + el.alen;
	wiggle = el.maxNormalISize;
      }
    } 
  }

  // Update clique, deletions
  template<typename TBamRecord, typename TSize>
  inline bool 
  _updateClique(TBamRecord const& el, TSize& svStart, TSize& svEnd, TSize& wiggle, int32_t const svt) 
  {
    if (_translocation(svt)) {
      int ct = _getSpanOrientation(svt);
      TSize newSvStart;
      TSize newSvEnd;
      TSize newWiggle = wiggle;
      if (ct%2==0) {
	newSvStart = std::max(svStart, el.pos + el.alen);
	newWiggle -= (newSvStart - svStart);
	if (ct>=2) {
	  newSvEnd = std::min(svEnd, el.mpos);
	  newWiggle -= (svEnd - newSvEnd);
	} else  {
	  newSvEnd = std::max(svEnd, el.mpos + el.malen);
	  newWiggle -= (newSvEnd - svEnd);
	}
      } else {
	newSvStart = std::min(svStart, el.pos);
	newWiggle -= (svStart - newSvStart);
	if (ct>=2) {
	  newSvEnd = std::max(svEnd, el.mpos + el.malen);
	  newWiggle -= (newSvEnd - svEnd);
	} else {
	  newSvEnd = std::min(svEnd, el.mpos);
	  newWiggle -= (svEnd - newSvEnd);
	}
      }
      // Is this still a valid translocation cluster?
      if (newWiggle>0) {
	svStart = newSvStart;
	svEnd = newSvEnd;
	wiggle = newWiggle;
	return true;
      }
      return false;
    } else {
      if ((svt == 0) || (svt == 1)) { 
	int ct = _getSpanOrientation(svt);
	TSize newSvStart;
	TSize newSvEnd;
	TSize newWiggle;
	TSize wiggleChange;
	if (!ct) {
	  newSvStart = std::max(svStart, el.mpos + el.malen);
	  newSvEnd = std::max(svEnd, el.pos + el.alen);
	  newWiggle = std::min(el.maxNormalISize - (newSvStart - el.mpos), el.maxNormalISize - (newSvEnd - el.pos));
	  wiggleChange = wiggle - std::max(newSvStart - svStart, newSvEnd - svEnd);
	} else {
	  newSvStart = std::min(svStart, el.mpos);
	  newSvEnd = std::min(svEnd, el.pos);
	  newWiggle = std::min(el.maxNormalISize - (el.mpos + el.malen - newSvStart), el.maxNormalISize - (el.pos + el.alen - newSvEnd));
	  wiggleChange = wiggle - std::max(svStart - newSvStart, svEnd - newSvEnd);
	}
	if (wiggleChange < newWiggle) newWiggle=wiggleChange;
	
	// Does the new inversion size agree with all pairs
	if ((newSvStart < newSvEnd) && (newWiggle>=0)) {
	  svStart = newSvStart;
	  svEnd = newSvEnd;
	  wiggle = newWiggle;
	  return true;
	}
	return false;
      } else if (svt == 2) {
	TSize newSvStart = std::max(svStart, el.mpos + el.malen);
	TSize newSvEnd = std::min(svEnd, el.pos);
	TSize newWiggle = el.pos + el.alen - el.mpos - el.maxNormalISize - (newSvEnd - newSvStart);
	TSize wiggleChange = wiggle + (svEnd-svStart) - (newSvEnd - newSvStart);
	if (wiggleChange > newWiggle) newWiggle=wiggleChange;
	
	// Does the new deletion size agree with all pairs
	if ((newSvStart < newSvEnd) && (newWiggle<=0)) {
	  svStart = newSvStart;
	  svEnd = newSvEnd;
	  wiggle = newWiggle;
	  return true;
	}
	return false;
      } else if (svt == 3) {
	TSize newSvStart = std::min(svStart, el.mpos);
	TSize newSvEnd = std::max(svEnd, el.pos + el.alen);
	TSize newWiggle = el.pos - (el.mpos + el.malen) + el.maxNormalISize - (newSvEnd - newSvStart);
	TSize wiggleChange = wiggle - ((newSvEnd - newSvStart) - (svEnd-svStart));
	if (wiggleChange < newWiggle) newWiggle = wiggleChange;
	
	// Does the new duplication size agree with all pairs
	if ((newSvStart < newSvEnd) && (newWiggle>=0)) {
	  svStart = newSvStart;
	  svEnd = newSvEnd;
	  wiggle = newWiggle;
	  return true;
	}
	return false;
      }
    }
    return false;
  }


  template<typename TCompEdgeList>
  inline void
  _searchCliques(TCompEdgeList& compEdge, std::vector<SRBamRecord>& br, std::vector<StructuralVariantRecord>& sv, uint32_t const wiggle, int32_t const svt) {
    typedef typename TCompEdgeList::mapped_type TEdgeList;
    typedef typename TEdgeList::value_type TEdgeRecord;
    typedef typename TEdgeRecord::TVertexType TVertex;

    // Iterate all components
    for(typename TCompEdgeList::iterator compIt = compEdge.begin(); compIt != compEdge.end(); ++compIt) {
      // Sort edges by weight
      std::sort(compIt->second.begin(), compIt->second.end(), SortSREdgeRecords<TEdgeRecord>());

      // Find a large clique
      typename TEdgeList::const_iterator itWEdge = compIt->second.begin();
      typename TEdgeList::const_iterator itWEdgeEnd = compIt->second.end();
      typedef std::set<TVertex> TCliqueMembers;
      TCliqueMembers clique;
      TCliqueMembers incompatible;
      
      // Initialize clique
      clique.insert(itWEdge->source);
      int32_t chr = br[itWEdge->source].chr;
      int32_t chr2 = br[itWEdge->source].chr2;
      int32_t ciposlow = br[itWEdge->source].pos;
      uint64_t pos = br[itWEdge->source].pos;
      int32_t ciposhigh = br[itWEdge->source].pos; 
      int32_t ciendlow = br[itWEdge->source].pos2;
      uint64_t pos2 = br[itWEdge->source].pos2;
      int32_t ciendhigh = br[itWEdge->source].pos2;
      int32_t inslen = br[itWEdge->source].inslen;

      // Grow clique
      bool cliqueGrow = true;
      while (cliqueGrow) {
	itWEdge = compIt->second.begin();
	cliqueGrow = false;
	// Find next best edge for extension
	for(;(!cliqueGrow) && (itWEdge != itWEdgeEnd);++itWEdge) {
	  TVertex v;
	  if ((clique.find(itWEdge->source) == clique.end()) && (clique.find(itWEdge->target) != clique.end())) v = itWEdge->source;
	  else if ((clique.find(itWEdge->source) != clique.end()) && (clique.find(itWEdge->target) == clique.end())) v = itWEdge->target;
	  else continue;
	  if (incompatible.find(v) != incompatible.end()) continue;
	  // Try to update clique with this vertex
	  int32_t newCiPosLow = std::min(br[v].pos, ciposlow);
	  int32_t newCiPosHigh = std::max(br[v].pos, ciposhigh);
	  int32_t newCiEndLow = std::min(br[v].pos2, ciendlow);
	  int32_t newCiEndHigh = std::max(br[v].pos2, ciendhigh);
	  if (((newCiPosHigh - newCiPosLow) < (int32_t) wiggle) && ((newCiEndHigh - newCiEndLow) < (int32_t) wiggle)) cliqueGrow = true;
	  if (cliqueGrow) {
	    // Accept new vertex
	    clique.insert(v);
	    ciposlow = newCiPosLow;
	    pos += br[v].pos;
	    ciposhigh = newCiPosHigh;
	    ciendlow = newCiEndLow;
	    pos2 += br[v].pos2;
	    ciendhigh = newCiEndHigh;
	    inslen += br[v].inslen;
	  } else incompatible.insert(v);
	}
      }

      // At least 2 split reads?
      if (clique.size()>1) {
	int32_t svStart = (int32_t) (pos / (uint64_t) clique.size());
	int32_t svEnd = (int32_t) (pos2 / (uint64_t) clique.size());
	int32_t svInsLen = (int32_t) (inslen / (int32_t) clique.size());
	if ((ciposlow > svStart) || (ciposhigh < svStart) || (ciendlow > svEnd) || (ciendhigh < svEnd)) {
	  std::cerr << "Warning: Confidence intervals out of bounds: " << ciposlow << ',' << svStart << ',' << ciposhigh << ':' << ciendlow << ',' << svEnd << ',' << ciendhigh << std::endl;
	}
	int32_t svid = sv.size();
	sv.push_back(StructuralVariantRecord(chr, svStart, chr2, svEnd, (ciposlow - svStart), (ciposhigh - svStart), (ciendlow - svEnd), (ciendhigh - svEnd), clique.size(), svInsLen, svt, svid));
	// Reads assigned
	for(typename TCliqueMembers::iterator itC = clique.begin(); itC != clique.end(); ++itC) br[*itC].svid = svid;
      }
    }
  }
  

  template<typename TConfig>
  inline void
  cluster(TConfig const& c, std::vector<SRBamRecord>& br, std::vector<StructuralVariantRecord>& sv, uint32_t const varisize, int32_t const svt) {
    uint32_t count = 0;
    for(int32_t refIdx = 0; refIdx < c.nchr; ++refIdx) {
      
      // Components
      typedef std::vector<uint32_t> TComponent;
      TComponent comp;
      comp.resize(br.size(), 0);
      uint32_t numComp = 0;

      // Edge lists for each component
      typedef uint32_t TWeightType;
      typedef uint32_t TVertex;
      typedef SREdgeRecord<TWeightType, TVertex> TEdgeRecord;
      typedef std::vector<TEdgeRecord> TEdgeList;
      typedef std::map<uint32_t, TEdgeList> TCompEdgeList;
      TCompEdgeList compEdge;

	
      std::size_t lastConnectedNode = 0;
      std::size_t lastConnectedNodeStart = 0;
      for(uint32_t i = 0; i<br.size(); ++i) {
	if (br[i].chr == refIdx) {
	  ++count;
	  // Safe to clean the graph?
	  if (i > lastConnectedNode) {
	    // Clean edge lists
	    if (!compEdge.empty()) {
	      // Search cliques
	      _searchCliques(compEdge, br, sv, varisize, svt);
	      lastConnectedNodeStart = lastConnectedNode;
	      compEdge.clear();
	    }
	  }
	  
	  
	  for(uint32_t j = i + 1; j<br.size(); ++j) {
	    if (br[j].chr == refIdx) {
	      if ( (uint32_t) (br[j].pos - br[i].pos) > varisize) break;
	      if ( (uint32_t) std::abs(br[j].pos2 - br[i].pos2) < varisize) {
		// Update last connected node
		if (j > lastConnectedNode) lastConnectedNode = j;
		
		// Assign components
		uint32_t compIndex = 0;
		if (!comp[i]) {
		  if (!comp[j]) {
		    // Both vertices have no component
		    compIndex = ++numComp;
		    comp[i] = compIndex;
		    comp[j] = compIndex;
		    compEdge.insert(std::make_pair(compIndex, TEdgeList()));
		  } else {
		    compIndex = comp[j];
		    comp[i] = compIndex;
		  }	
		} else {
		  if (!comp[j]) {
		    compIndex = comp[i];
		    comp[j] = compIndex;
		  } else {
		    // Both vertices have a component
		    if (comp[j] == comp[i]) {
		      compIndex = comp[j];
		    } else {
		      // Merge components
		      compIndex = comp[i];
		      uint32_t otherIndex = comp[j];
		      if (otherIndex < compIndex) {
			compIndex = comp[j];
			otherIndex = comp[i];
		      }
		      // Re-label other index
		      for(uint32_t k = lastConnectedNodeStart; k <= lastConnectedNode; ++k) {
			if (otherIndex == comp[k]) comp[k] = compIndex;
		      }
		      // Merge edge lists
		      TCompEdgeList::iterator compEdgeIt = compEdge.find(compIndex);
		      TCompEdgeList::iterator compEdgeOtherIt = compEdge.find(otherIndex);
		      compEdgeIt->second.insert(compEdgeIt->second.end(), compEdgeOtherIt->second.begin(), compEdgeOtherIt->second.end());
		      compEdge.erase(compEdgeOtherIt);
		    }
		  }
		}
		
		// Append new edge
		TCompEdgeList::iterator compEdgeIt = compEdge.find(compIndex);
		if (compEdgeIt->second.size() < c.graphPruning) {
		  // Breakpoint distance
		  TWeightType weight = std::abs(br[j].pos2 - br[i].pos2) + std::abs(br[j].pos - br[i].pos);
		  compEdgeIt->second.push_back(TEdgeRecord(i, j, weight));
		}
	      }
	    }
	  }
	}
      }
      // Search cliques
      if (!compEdge.empty()) {
	_searchCliques(compEdge, br, sv, varisize, svt);
	compEdge.clear();
      }
    }
  }


  template<typename TCompEdgeList, typename TBamRecord, typename TSVs>
  inline void
  _searchCliques(TCompEdgeList& compEdge, TBamRecord const& bamRecord, TSVs& svs, int32_t const svt) {
    typedef typename TCompEdgeList::mapped_type TEdgeList;
    typedef typename TEdgeList::value_type TEdgeRecord;

    // Iterate all components
    for(typename TCompEdgeList::iterator compIt = compEdge.begin(); compIt != compEdge.end(); ++compIt) {
      // Sort edges by weight
      std::sort(compIt->second.begin(), compIt->second.end(), SortEdgeRecords<TEdgeRecord>());
      
      // Find a large clique
      typename TEdgeList::const_iterator itWEdge = compIt->second.begin();
      typename TEdgeList::const_iterator itWEdgeEnd = compIt->second.end();
      typedef std::set<std::size_t> TCliqueMembers;
      
      TCliqueMembers clique;
      TCliqueMembers incompatible;
      int32_t svStart = -1;
      int32_t svEnd = -1;
      int32_t wiggle = 0;
      int32_t clusterRefID=bamRecord[itWEdge->source].tid;
      int32_t clusterMateRefID=bamRecord[itWEdge->source].mtid;
      _initClique(bamRecord[itWEdge->source], svStart, svEnd, wiggle, svt);
      if ((clusterRefID==clusterMateRefID) && (svStart >= svEnd))  continue;
      clique.insert(itWEdge->source);
      
      // Grow the clique from the seeding edge
      bool cliqueGrow=true;
      while (cliqueGrow) {
	itWEdge = compIt->second.begin();
	cliqueGrow = false;
	for(;(!cliqueGrow) && (itWEdge != itWEdgeEnd);++itWEdge) {
	  std::size_t v;
	  if ((clique.find(itWEdge->source) == clique.end()) && (clique.find(itWEdge->target) != clique.end())) v = itWEdge->source;
	  else if ((clique.find(itWEdge->source) != clique.end()) && (clique.find(itWEdge->target) == clique.end())) v = itWEdge->target;
	  else continue;
	  if (incompatible.find(v) != incompatible.end()) continue;
	  cliqueGrow = _updateClique(bamRecord[v], svStart, svEnd, wiggle, svt);
	  if (cliqueGrow) clique.insert(v);
	  else incompatible.insert(v);
	}
      }
      
      if ((clique.size()>1) && (_svSizeCheck(svStart, svEnd, svt))) {
	StructuralVariantRecord svRec;
	svRec.chr = clusterRefID;
	svRec.chr2 = clusterMateRefID;
	svRec.svStart = (uint32_t) svStart + 1;
	svRec.svEnd = (uint32_t) svEnd + 1;
	svRec.peSupport = clique.size();
	int32_t ci_wiggle = std::max(abs(wiggle), 50);
	svRec.ciposlow = -ci_wiggle;
	svRec.ciposhigh = ci_wiggle;
	svRec.ciendlow = -ci_wiggle;
	svRec.ciendhigh = ci_wiggle;
	std::vector<uint8_t> mapQV;
	for(typename TCliqueMembers::const_iterator itC = clique.begin(); itC!=clique.end(); ++itC) mapQV.push_back(bamRecord[*itC].MapQuality);
	std::sort(mapQV.begin(), mapQV.end());
	svRec.peMapQuality = mapQV[mapQV.size()/2];
	svRec.srSupport=0;
	svRec.srAlignQuality=0;
	svRec.precise=false;
	svRec.svt = svt;
	svRec.insLen = 0;
	svRec.homLen = 0;
	svs.push_back(svRec);
      }
    }
  }
  
  

  template<typename TConfig>
  inline void
  cluster(TConfig const& c, std::vector<BamAlignRecord>& bamRecord, std::vector<StructuralVariantRecord>& svs, uint32_t const varisize, int32_t const svt) {
    typedef typename std::vector<BamAlignRecord> TBamRecord;
    // Components
    typedef std::vector<uint32_t> TComponent;
    TComponent comp;
    comp.resize(bamRecord.size(), 0);
    uint32_t numComp = 0;
      
    // Edge lists for each component
    typedef uint8_t TWeightType;
    typedef EdgeRecord<TWeightType, std::size_t> TEdgeRecord;
    typedef std::vector<TEdgeRecord> TEdgeList;
    typedef std::map<uint32_t, TEdgeList> TCompEdgeList;
    TCompEdgeList compEdge;
    
    // Iterate the chromosome range
    std::size_t lastConnectedNode = 0;
    std::size_t lastConnectedNodeStart = 0;
    std::size_t bamItIndex = 0;
    for(TBamRecord::const_iterator bamIt = bamRecord.begin(); bamIt != bamRecord.end(); ++bamIt, ++bamItIndex) {
      // Safe to clean the graph?
      if (bamItIndex > lastConnectedNode) {
	// Clean edge lists
	if (!compEdge.empty()) {
	  _searchCliques(compEdge, bamRecord, svs, svt);
	  lastConnectedNodeStart = lastConnectedNode;
	  compEdge.clear();
	}
      }
      int32_t const minCoord = _minCoord(bamIt->pos, bamIt->mpos, svt);
      int32_t const maxCoord = _maxCoord(bamIt->pos, bamIt->mpos, svt);
      TBamRecord::const_iterator bamItNext = bamIt;
      ++bamItNext;
      std::size_t bamItIndexNext = bamItIndex + 1;
      for(; ((bamItNext != bamRecord.end()) && (abs(_minCoord(bamItNext->pos, bamItNext->mpos, svt) + bamItNext->alen - minCoord) <= varisize)) ; ++bamItNext, ++bamItIndexNext) {
	  // Check that mate chr agree (only for translocations)
	if (bamIt->mtid != bamItNext->mtid) continue;
	
	// Check combinability of pairs
	if (_pairsDisagree(minCoord, maxCoord, bamIt->alen, bamIt->maxNormalISize, _minCoord(bamItNext->pos, bamItNext->mpos, svt), _maxCoord(bamItNext->pos, bamItNext->mpos, svt), bamItNext->alen, bamItNext->maxNormalISize, svt)) continue;
	
	// Update last connected node
	if (bamItIndexNext > lastConnectedNode ) lastConnectedNode = bamItIndexNext;
	
	// Assign components
	uint32_t compIndex = 0;
	if (!comp[bamItIndex]) {
	  if (!comp[bamItIndexNext]) {
	    // Both vertices have no component
	    compIndex = ++numComp;
	    comp[bamItIndex] = compIndex;
	    comp[bamItIndexNext] = compIndex;
	    compEdge.insert(std::make_pair(compIndex, TEdgeList()));
	  } else {
	    compIndex = comp[bamItIndexNext];
	    comp[bamItIndex] = compIndex;
	  }
	} else {
	  if (!comp[bamItIndexNext]) {
	    compIndex = comp[bamItIndex];
	    comp[bamItIndexNext] = compIndex;
	  } else {
	    // Both vertices have a component
	    if (comp[bamItIndexNext] == comp[bamItIndex]) {
	      compIndex = comp[bamItIndexNext];
	    } else {
	      // Merge components
	      compIndex = comp[bamItIndex];
	      uint32_t otherIndex = comp[bamItIndexNext];
	      if (otherIndex < compIndex) {
		compIndex = comp[bamItIndexNext];
		otherIndex = comp[bamItIndex];
	      }
	      // Re-label other index
	      for(std::size_t i = lastConnectedNodeStart; i <= lastConnectedNode; ++i) {
		if (otherIndex == comp[i]) comp[i] = compIndex;
	      }
	      // Merge edge lists
	      TCompEdgeList::iterator compEdgeIt = compEdge.find(compIndex);
	      TCompEdgeList::iterator compEdgeOtherIt = compEdge.find(otherIndex);
	      compEdgeIt->second.insert(compEdgeIt->second.end(), compEdgeOtherIt->second.begin(), compEdgeOtherIt->second.end());
	      compEdge.erase(compEdgeOtherIt);
	    }
	  }
	}
	
	// Append new edge
	TCompEdgeList::iterator compEdgeIt = compEdge.find(compIndex);
	if (compEdgeIt->second.size() < c.graphPruning) {
	  TWeightType weight = (TWeightType) ( std::log((double) abs( abs( (_minCoord(bamItNext->pos, bamItNext->mpos, svt) - minCoord) - (_maxCoord(bamItNext->pos, bamItNext->mpos, svt) - maxCoord) ) - abs(bamIt->Median - bamItNext->Median)) + 1) / std::log(2) );
	  compEdgeIt->second.push_back(TEdgeRecord(bamItIndex, bamItIndexNext, weight));
	}
      }
    }
    if (!compEdge.empty()) {
      _searchCliques(compEdge, bamRecord, svs, svt);
      compEdge.clear();
    }
  }
  

    
}

#endif
