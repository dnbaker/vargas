/**
 * @author Ravi Gaddipati
 * @date June 21, 2016
 * rgaddip1@jhu.edu
 *
 * @brief
 * Provides tools to interact with Alignments.
 *
 * @details
 * Reads are aligned using
 * SIMD vectorized smith-waterman. Reads are grouped into batches, with
 * size determined by the optimal hardware support. The default template
 * parameters support an 8 bit score.
 *
 * @file
 */

#ifndef VARGAS_ALIGNMENT_H
#define VARGAS_ALIGNMENT_H

#include <vector>
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <string>
#include "utils.h"
#include "simdpp/simd.h"
#include "graph.h"
#include "doctest.h"


#define DEBUG_PRINT_SW 0 // Print the full SW matrix for each node aligned
#define DEBUG_PRINT_SW_NUM 0 // Print the matrix for this read number in the alignment group

#define ALIGN_ENABLE_PROFILE 1

#ifdef ALIGN_ENABLE_PROFILE

#include <unordered_map>
#include <ctime>
std::unordered_map<std::string, time_t> _PROF_TOTALS;
std::unordered_map<std::string, time_t> _PROF_STARTS;

#define PROF_BEGIN(name) { _PROF_STARTS[name] = std::clock();}
#define PROF_END(name) {_PROF_TOTALS[name] += std::clock() - _PROF_STARTS[name];}
#define PROF_TERMINATE { \
                        time_t TOT = 0; \
                        for (auto &p : _PROF_TOTALS) TOT += p.second; \
                        for (auto &p : _PROF_TOTALS) { \
                            std::cerr << p.first << ": " << ((double) p.second / (CLOCKS_PER_SEC/1000)) << "ms, " \
                            << ((double) (100 * p.second) / TOT) << "%" << std::endl; \
                        } \
                        _PROF_TOTALS.clear(); \
                        _PROF_STARTS.clear(); \
                        }

#else

#define PROF_BEGIN(name)
#define PROF_END(name)
#define PROF_TERMINATE

#endif

namespace Vargas {

  /**
   * @brief Main SIMD SW Aligner.
   * @details
   * Aligns a read batch to a reference sequence.
   * Note: "score" means something that is added, "penalty" refers to something
   * that is subtracted. All scores/penalties are provided as positive integers.
   * Most memory is allocated for the class so it can be reused during alignment. To reduce memory usage,
   * the maximum node size can be reduced. \n
   * Usage:\n
   * @code{.cpp}
   * #include "graph.h"
   * #include "alignment.h"
   *
   * Vargas::GraphBuilder gb("reference.fa", "var.bcf");
   * gb.node_len(5);
   * gb.ingroup(100);
   * gb.region("x:0-15");
   *
   * Vargas::Graph g = gb.build();
   * std::vector<std::string> reads = {"ACGT", "GGGG", "ATTA", "CCNT"};
   *
   * Vargas::Aligner<> a(g.max_node_len(), 4);
   * Vargas::Aligner<>::Results res = a.align(reads, g.begin(), g.end());
   *
   * for (int i = 0; i < reads.size(); ++i) {
   *    std::cout << reads[i] << ", score:" << res.max_score[i] << " pos:" << res.max_pos[i] << std::endl;
   * }
   * // Output:
   * // ACGT, score:8 pos:10
   * // GGGG, score:6 pos:65
   * // ATTA, score:8 pos:18
   * // CCNT, score:8 pos:80
   * @endcode
   */

  class ByteAligner {
    public:
      const int num_reads = SIMDPP_FAST_INT8_SIZE;
      typedef simdpp::uint8 CellType;
      typedef uint8_t NativeT;
      typedef typename std::vector<CellType<num_reads>> VecType;

      /**
       * @brief
       * Default constructor uses the following score values: \n
       * Match : 2 \n
       * Mismatch : -2 \n
       * Gap Open : 3 \n
       * Gap Extend : 1 \n
       * @param max_node_len maximum node length
       * @param rlen maximum read length
       */
      ByteAligner(size_t max_node_len, int rlen) :
          read_len(rlen),
          _max_node_len(max_node_len) { _alloc(); }

      /**
       * @brief
       * Set scoring parameters.
       * @param max_node_len max node length
       * @param rlen max read length
       * @param match match score
       * @param mismatch mismatch penalty
       * @param open gap open penalty
       * @param extend gap extend penalty
       */
      ByteAligner(size_t max_node_len,
                  int rlen,
                  uint8_t match,
                  uint8_t mismatch,
                  uint8_t open,
                  uint8_t extend) :
          read_len(rlen),
          _match(match), _mismatch(mismatch), _gap_open(open), _gap_extend(extend),
          _max_node_len(max_node_len) { _alloc(); }

      ~ByteAligner() {
          _dealloc();
      }

      /**
       * @brief
       * Container for a packaged batch of reads.
       * @details
       * Reads are interleaved so each SIMD vector
       * contains bases from all reads, respective to the base number. For example AlignmentGroup[0]
       * would contain the first bases of every read. All reads must be the same length. Minimal error checking.
       * @tparam num_reads max number of reads. If a non-default T is used, this should be set to
       *    SIMDPP_FAST_T_SIZE where T corresponds to the width of T. For ex. Default T=simdpp::uint8 uses
       *    SIMDPP_FAST_INT8_SIZE
       * @tparam T element type
       */
      class AlignmentGroup {
        public:

          AlignmentGroup() { }

          /**
           * @brief
           * Read length is set to first read size.
           * @param batch package the given vector of reads. Must be nonempty.
           */
          AlignmentGroup(const std::vector<std::vector<Base>> &batch) {
              load_reads(batch);
          }

          /**
           * @brief
           * Read length is set to first read size.
           * @param batch package the given vector of reads. Must be nonempty.
           */
          AlignmentGroup(const std::vector<std::string> &batch) {
              load_reads(batch);
          }

          /**
           * @param batch load the given vector of reads.
           */
          __INLINE__ void load_reads(const std::vector<std::string> &batch) {
              std::vector<std::vector<Base>> _reads;
              for (auto &b : batch) _reads.push_back(seq_to_num(b));
              load_reads(_reads);
          }

          /**
           * @param batch load the given vector of reads.
           */
          __INLINE__ void load_reads(const std::vector<std::vector<Base>> &batch) {
              read_len = batch[0].size();
              _package_reads(batch);
          }

          /**
           * @brief
           * Return the i'th base of every read in a simdpp vector.
           * @param i base index.
           */
          const CellType<num_reads> &at(int i) const {
              return _packaged_reads.at(i);
          }

          /**
           * @brief
           * Pointer to raw packaged read data.
           * @return CellType<num_reads> pointer
           */
          const CellType<num_reads> *data() const {
              return _packaged_reads.data();
          }

          /**
           * @brief
           * Non const version of at(i).
           * @param i base index
           */
          CellType<num_reads> &operator[](int i) {
              return _packaged_reads.at(i);
          }

          /**
           * @brief
           * Returns optimal number of reads in a batch based on SIMD architecture.
           * @return batch size.
           */
          size_t group_size() const { return num_reads; }

          typename std::vector<CellType<num_reads>>::const_iterator begin() const { return _packaged_reads.begin(); }
          typename std::vector<CellType<num_reads>>::const_iterator end() const { return _packaged_reads.end(); }

        private:

          int read_len;

          /**
           * _packaged_reads[i] contains all i'th bases.
           * The length of _packaged_reads is the length of the read,
           * where as the length of _packaged_reads[i] is the number
           * of reads.
           */
          std::vector<CellType<num_reads>> _packaged_reads;

          /**
           * Interleaves reads so all same-index base positions are in one
           * vector. Empty spaces are padded with Base::N.
           * @param _reads vector of reads to package
           */
          __INLINE__ void _package_reads(const std::vector<std::vector<Base>> &_reads) {
              PROF_BEGIN("Package Reads")
              _packaged_reads.resize(read_len);

              // allocate memory
              uchar **pckg = (uchar **) malloc(read_len * sizeof(uchar *));
              for (int i = 0; i < read_len; ++i) {
                  pckg[i] = (uchar *) malloc(num_reads * sizeof(uchar));
              }

              // Interleave reads
              // For each read (read[i] is in _packaged_reads[0..n][i]
              for (size_t r = 0; r < _reads.size(); ++r) {
                  assert(_reads[r].size() == read_len);
                  // Put each base in the appropriate vector element
                  for (size_t p = 0; p < read_len; ++p) {
                      pckg[p][r] = _reads[r][p];
                  }
              }

              // Pad underful batches
              for (size_t r = _reads.size(); r < num_reads; ++r) {
                  for (size_t p = 0; p < read_len; ++p) {
                      pckg[p][r] = Base::N;
                  }
              }

              // Load into vectors
              for (int i = 0; i < read_len; ++i) {
                  _packaged_reads[i] = simdpp::load(pckg[i]);
                  free(pckg[i]);
              }
              free(pckg);

              PROF_END("Package Reads")
          }

      };

      /**
       * @brief
       * Struct to return the alignment results
       */
      struct Results {
          std::vector<uint32_t> best_pos;
          /**< Best positions */
          std::vector<uint32_t> sub_pos;
          /**< Second best positions */
          std::vector<uint8_t> best_count;
          /**< Occurances of best_pos */
          std::vector<uint8_t> sub_count;
          /**< Occurances of sub_pos */
          std::vector<uint8_t> cor_flag;    /**< 1 if best alignment matched target, 2 for second best, else 0 */

          std::vector<NativeT> best_score;
          /**< Best scores */
          std::vector<NativeT> sub_score;   /**< Second best scores */

          /**
           * @brief
           * Resize all result vectors.
           */
          void resize(size_t size) {
              best_pos.resize(size);
              sub_pos.resize(size);
              best_count.resize(size);
              sub_count.resize(size);
              best_score.resize(size);
              sub_score.resize(size);
              cor_flag.resize(size);
          }
      };

      /**
       * Set the scoring scheme used for the alignments.
       * @param match match score
       * @param mismatch mismatch score
       * @param open gap open penalty
       * @param extend gap extend penalty
       */
      void set_scores(int8_t match,
                      int8_t mismatch,
                      int8_t open,
                      int8_t extend) {
          _match = match;
          _mismatch = mismatch;
          _gap_open = open;
          _gap_extend = extend;
      }

      /**
       * @brief
       * Align a batch of reads to a graph range, return a vector of alignments
       * corresponding to the reads.
       * @param read_group vector of reads to align to
       * @param begin iterator to beginning of graph
       * @param end iterator to end of graph
       * @return Results packet
       */
      Results align(const std::vector<std::string> &read_group,
                    Graph::FilteringIter begin,
                    Graph::FilteringIter end) {
          Results aligns;
          align_into(read_group, begin, end, aligns);
          return aligns;
      }

      /**
       * @brief
       * Align a batch of reads to a graph range, return a vector of alignments
       * corresponding to the reads.
       * @param read_group vector of reads to align to
       * @param targets correct positions for reads in read_group
       * @param begin iterator to beginning of graph
       * @param end iterator to end of graph
       * @param aligns Results packet to populate
       */
      void align_into(const std::vector<std::string> &read_group,
                      Graph::FilteringIter begin,
                      Graph::FilteringIter end,
                      Results &aligns) {
          using namespace simdpp;

          if (!validate(begin, end)) return;

          const size_t read_group_size = read_group.size();
          const int full_groups = read_group_size / num_reads;

          aligns.resize((full_groups + 1) * num_reads); // Need space for padded reads too

          std::unordered_map<uint32_t, _seed> seed_map;

          for (int i = 0; i <= full_groups; ++i) {
              auto rg_begin = read_group.begin() + (i * num_reads);
              auto len = (i * num_reads) + num_reads >= read_group_size ? read_group_size - (i * num_reads) : num_reads;
              auto rg_subset = std::vector<std::string>(rg_begin, rg_begin + len);
              _ag.load_reads(rg_subset);

              // Reset old info
              max_score = ZERO_CT;
              sub_score = ZERO_CT;
              // fill into result vectors
              max_pos = aligns.best_pos.data() + (i * num_reads);
              sub_pos = aligns.sub_pos.data() + (i * num_reads);
              max_count = aligns.best_count.data() + (i * num_reads);
              sub_count = aligns.sub_count.data() + (i * num_reads);
              cor_flag = aligns.cor_flag.data() + (i * num_reads);

              _seed seed(read_len);

              seed_map.clear();
              for (auto &gi = begin; gi != end; ++gi) {
                  _get_seed(gi.incoming(), seed_map, &seed);
                  if (gi->is_pinched()) seed_map.clear();
                  seed_map.emplace(gi->id(), _fill_node(*gi, _ag, &seed));
              }

              memcpy(aligns.best_score.data() + (i * num_reads), &max_score, len * sizeof(NativeT));
              memcpy(aligns.sub_score.data() + (i * num_reads), &sub_score, len * sizeof(NativeT));
          }

          aligns.resize(read_group_size); // crop off extra space
          PROF_TERMINATE
      }

      bool validate(Graph::FilteringIter begin,
                    Graph::FilteringIter end) {
          std::unordered_map<size_t> filled;
          bool ret = true;
          for (auto &gi = begin; gi != end; ++gi) {
              for (auto i : gi.incoming()) {
                  if (filled.count(i) == 0) {
                      ret = false;
                      std::cerr << "Node (ID:" << gi->id() << ", POS:" << gi->end() << ")"
                          << " hit before previous node " << i << std::endl;
                  }
              }
          }
          return ret;
      }
    private:
      /**
       * @brief
       * Ending vectors from a previous node
       */
      struct _seed {
          _seed(int read_len) : S_col(read_len), I_col(read_len) { }
          VecType S_col;
          /**< Last column of score matrix.*/
          VecType I_col;    /**< Last column of I vector.*/
      };

      /**
       * @brief
       * Returns the best seed from all previous nodes.
       * @param prev_ids All nodes preceding current node
       * @param seed_map ID->seed map for all previous nodes
       * @param seed best seed to populate
       */
      void _get_seed(const std::vector<uint32_t> &prev_ids,
                     const std::unordered_map<uint32_t, _seed> &seed_map,
                     _seed *seed) {
          PROF_BEGIN("Get Seed")
          using namespace simdpp;

          const _seed *ns;

          try {
              for (size_t i = 0; i < read_len; ++i) {
                  seed->I_col[i] = ZERO_CT;
                  seed->S_col[i] = ZERO_CT;
                  for (uint32_t id : prev_ids) {
                      ns = &seed_map.at(id);
                      seed->I_col[i] = max(seed->I_col[i], ns->I_col[i]);
                      seed->S_col[i] = max(seed->S_col[i], ns->S_col[i]);
              }
          }
      }
          catch (std::exception &e) {
              throw std::logic_error("Unable to get seed, invalid node ordering.");
          }
          PROF_BEGIN("Get Seed")
      }

      /**
       * @brief
       * deletes allocated matrix filling vectors.
       */
      void _dealloc() {
          if (Sa) delete Sa;
          if (Sb) delete Sb;
          if (Da) delete Da;
          if (Db) delete Db;
          if (Ia) delete Ia;
          if (Ib) delete Ib;

          Sa = nullptr;
          Sb = nullptr;
          Da = nullptr;
          Db = nullptr;
          Ia = nullptr;
          Ib = nullptr;

          S_prev = nullptr;
          S_curr = nullptr;
          D_prev = nullptr;
          D_curr = nullptr;
          I_prev = nullptr;
          I_curr = nullptr;
      }

      /**
       * @brief
       * Allocate S and D vectors. I is determined by template parameter.
       */
      void _alloc() {
          Sa = new VecType(_max_node_len);
          Sb = new VecType(_max_node_len);
          Da = new VecType(_max_node_len);
          Db = new VecType(_max_node_len);
          Ia = new VecType(read_len);
          Ib = new VecType(read_len);

          S_prev = Sa->data();
          S_curr = Sb->data();
          D_prev = Da->data();
          D_curr = Db->data();
          I_prev = Ia->data();
          I_curr = Ib->data();
      }

      /**
       * @brief
       * Computes local alignment of the node, with no previous seed.
       * @param n Node to align to
       * @param read_group AlignmentGroup to align
       */
      _seed _fill_node(const Graph::Node &n,
                       const AlignmentGroup &read_group) {
          _seed s(read_len);
          s = _fill_node(n, read_group, &s);
          return s;
      }

      /**
       * @brief
       * Computes local alignment to the node.
       * @param n Node to align to
       * @param read_group AlignmentGroup to align
       * @param s seeds from previous nodes
       */
      _seed _fill_node(const Graph::Node &n,
                       const AlignmentGroup &read_group,
                       const _seed *s) {

          assert(n.seq().size() < _max_node_len);

          _seed nxt(read_len);  // Seed for next node
          auto node_seq = n.seq().data();
          const CellType<num_reads> *read_ptr = read_group.data();
          size_t seq_size = n.seq().size();
          uint32_t node_origin = n.end() - seq_size;

          #if DEBUG_PRINT_SW
          std::cout << std::endl << "-\t";
              for (auto c : n.seq_str()) std::cout << c << '\t';
              std::cout << std::endl;
          #endif

          // top left corner
          _fill_cell_rzcz(read_ptr[0], node_seq[0], s);
          _fill_cell_finish(0, 0, node_origin);

          // top row
          for (uint32_t c = 1; c < seq_size; ++c) {
              _fill_cell_rz(read_ptr[0], node_seq[c], c);
              _fill_cell_finish(0, c, node_origin);
          }

          #if DEBUG_PRINT_SW
          std::cout << num_to_base((Base) simdpp::extract<DEBUG_PRINT_SW_NUM>(read_ptr[0])) << '\t';
              for (size_t i = 0; i < n.seq().size(); ++i)
                  std::cout << (int) simdpp::extract<DEBUG_PRINT_SW_NUM>(S_curr[i]) << '\t';
              std::cout << "MAX| ";
              for (auto p : max_pos) std::cout << p << '\t';
              std::cout << "SUB| ";
              for (auto p : sub_pos) std::cout << p << '\t';
              std::cout << std::endl;
          #endif

          nxt.S_col[0] = S_curr[seq_size - 1];

          // Rest of the rows
          for (uint32_t r = 1; r < read_len; ++r) {
              // Swap the rows we are filling in. The previous row/col becomes what we fill in.
              swp_tmp = S_prev;
              S_prev = S_curr;
              S_curr = swp_tmp;

              swp_tmp = D_prev;
              D_prev = D_curr;
              D_curr = swp_tmp;

              swp_tmp = I_prev;
              I_prev = I_curr;
              I_curr = swp_tmp;

              // first col
              _fill_cell_cz(read_ptr[r], node_seq[0], r, s);
              _fill_cell_finish(r, 0, node_origin);

              // Inner grid
              for (uint32_t c = 1; c < seq_size; ++c) {
                  _fill_cell(read_ptr[r], node_seq[c], r, c);
                  _fill_cell_finish(r, c, node_origin);
              }

              nxt.S_col[r] = S_curr[seq_size - 1];

              #if DEBUG_PRINT_SW
              std::cout << num_to_base((Base) simdpp::extract<DEBUG_PRINT_SW_NUM>(read_ptr[r])) << '\t';
                  for (size_t i = 0; i < n.seq().size(); ++i)
                      std::cout << (int) simdpp::extract<DEBUG_PRINT_SW_NUM>(S_curr[i]) << '\t';
                  std::cout << "MAX| ";
                  for (auto p : max_pos) std::cout << p << '\t';
                  std::cout << "SUB| ";
                  for (auto p : sub_pos) std::cout << p << '\t';
                  std::cout << std::endl;
              #endif

      }

          // origin vector of what is now I_curr
          nxt.I_col = (Ia->data() == I_curr) ? *Ia : *Ib;
          return nxt;
      }

      /**
       * @brief
       * Fills the top left cell.
       * @param read_base ReadBatch vector
       * @param ref reference sequence base
       * @param s alignment seed from previous node
       */
      __INLINE__
      void _fill_cell_rzcz(const CellType<num_reads> &read_base,
                           const Base &ref,
                           const _seed *s) {
          _D(0, ZERO_CT, ZERO_CT);
          _I(0, s->S_col[0]);
          _M(0, read_base, ref, ZERO_CT);
      }

      /**
       * @brief
       * Fills cells when row is 0.
       * @param read_base ReadBatch vector
       * @param ref reference sequence base
       * @param col current column in matrix
       */
      __INLINE__
      void _fill_cell_rz(const CellType<num_reads> &read_base,
                         const Base &ref,
                         const uint32_t &col) {
          _D(col, ZERO_CT, ZERO_CT);
          _I(0, S_curr[col - 1]);
          _M(col, read_base, ref, ZERO_CT);
      }

      /**
       * @brief
       * Fills cells when col is 0.
       * @param read_base ReadBatch vector
       * @param ref reference sequence base
       * @param row current row in matrix
       * @param s alignment seed from previous node
       */
      __INLINE__
      void _fill_cell_cz(const CellType<num_reads> &read_base,
                         const Base &ref,
                         const uint32_t &row,
                         const _seed *s) {
          _D(0, D_prev[0], S_prev[0]);
          _I(row, s->S_col[row]);
          _M(0, read_base, ref, s->S_col[row - 1]);
      }

      /**
       * @brief
       * Fills the current cell.
       * @param read_base ReadBatch vector
       * @param ref reference sequence base
       * @param row current row in matrix
       * @param col current column in matrix
       */
      __INLINE__
      void _fill_cell(const CellType<num_reads> &read_base,
                      const Base &ref,
                      const uint32_t &row,
                      const uint32_t &col) {
          using namespace simdpp;

          _D(col, D_prev[col], S_prev[col]);
          _I(row, S_curr[col - 1]);
          _M(col, read_base, ref, S_prev[col - 1]);
      }

      /**
       * @brief
       * Score if there is a deletion
       * @param col current column
       * @param Dp Previous D value at current col.
       * @param Sp Previous S value at current col.
       */
      __INLINE__
      void _D(const uint32_t &col,
              const CellType<num_reads> &Dp,
              const CellType<num_reads> &Sp) {
          using namespace simdpp;
          PROF_BEGIN("D")

          // D(i,j) = D(i-1,j) - gap_extend
          // Dp is D_prev[col], 0 for row=0
          D_curr[col] = sub_sat(Dp, _gap_extend);   // tmp = S(i-1,j) - ( gap_open + gap_extend)
          // Sp is S_prev[col], 0 for row=0
          // D(i,j) = max{ D(i-1,j) - gap_extend, S(i-1,j) - ( gap_open + gap_extend) }
          tmp = sub_sat(Sp, _gap_extend + _gap_open);
          D_curr[col] = max(D_curr[col], tmp);

          PROF_END("D")
      }

      /**
       * @brief
       * Score if there is an insertion
       * @param row current row
       * @param Sc Previous S value (cell to the left)
       */
      __INLINE__
      void _I(const uint32_t &row,
              const CellType<num_reads> &Sc) {
          using namespace simdpp;
          PROF_BEGIN("I")

          // I(i,j) = I(i,j-1) - gap_extend
          I_curr[row] = sub_sat(I_prev[row], _gap_extend);  // I: I(i,j-1) - gap_extend
          // tmp = S(i,j-1) - (gap_open + gap_extend)
          // Sc is S_curr[col - 1], seed->S_col[row] for col=0
          tmp = sub_sat(Sc, _gap_extend + _gap_open);
          I_curr[row] = max(I_curr[row], tmp);

          PROF_END("I")
      }

      /**
       * @brief
       * Best score if there is a match/mismatch. Uses S_prev.
       * @param col current column
       * @param read read base vector
       * @param ref reference sequence base
       * @param Sp Previous S val at col-1 (upper left cell)
       */
      __INLINE__
      void _M(uint32_t col,
              const CellType<num_reads> &read,
              const Base &ref,
              const CellType<num_reads> &Sp) {
          using namespace simdpp;

          PROF_BEGIN("M")

          Ceq = ZERO_CT;
          Cneq = ZERO_CT;

          if (ref != Base::N) {
              // Set all mismatching pairs to _mismatch
              tmp = cmp_neq(read, ref);
              Cneq = tmp & _mismatch;   // If the read base is Base::N, set to 0 (Ceq)
              tmp = cmp_eq(read, Base::N);
              Cneq = blend(Ceq, Cneq, tmp);

              // b is not N, so all equal bases are valid
              tmp = cmp_eq(read, ref);
              Ceq = tmp & _match;
      }

          // Sp is S_prev[col - 1], 0 for row=0
          // Seed->S_col[row - 1] for col=0
          S_curr[col] = add_sat(Sp, Ceq);   // Add match scores
          S_curr[col] = sub_sat(S_curr[col], Cneq); // Subtract mismatch scores

          PROF_END("M")
      }


      /**
       * @brief
       * Takes the max of D,I, and M vectors and stores the current best score/position
       * Currently does not support non-deafault template args
       * @param row current row
       * @param col current column
       * @param node_origin Current position, used to get absolute alignment position
       */
      __INLINE__
      void _fill_cell_finish(const uint32_t &row,
                             const uint32_t &col,
                             const uint32_t &node_origin) {
          using namespace simdpp;

          curr = node_origin + col + 1;    // absolute position

          // S(i,j) = max{ D(i,j), I(i,j), S(i-1,j-1) + C(s,t) }
          S_curr[col] = max(D_curr[col], S_curr[col]);
          S_curr[col] = max(I_curr[row], S_curr[col]);

          // Check for new or equal high scores
          PROF_BEGIN("New Max")

          tmp = S_curr[col] > max_score;
          if (reduce_or(tmp)) {
              max_score = max(max_score, S_curr[col]);
              for (uchar i = 0; i < num_reads; ++i) {
                  // Check if the i'th elements MSB is set
                  if (extract(i, tmp)) {
                      max_elem = extract(i, max_score);
                      // If old max is larger than old sub_max, and if its far enough away
                      if (max_elem > extract(i, sub_score) && curr < max_pos[i] - read_len) {
                          insert(max_elem, i, sub_score);
                          sub_pos[i] = max_pos[i];
                          sub_count[i] = max_count[i];
                  }
                      max_pos[i] = curr;
                      max_count[i] = 0;
              }
          }
          }

          PROF_END("New Max")
          PROF_BEGIN("Eq Max")

          // Check for equal max score. If we set a new high score this will set the count to 1
          tmp = cmp_eq(S_curr[col], max_score);
          if (reduce_or(tmp)) {
              for (uchar i = 0; i < num_reads; ++i) {
                  // Check if the i'th elements MSB is set
                  if (extract(i, tmp)) {
                      max_count[i] += 1;
                      max_pos[i] = curr;
              }
          }
          }

          PROF_END("Eq Max")
          PROF_BEGIN("New Sub")

          // new second best score
          tmp = S_curr[col] > sub_score;
          if (reduce_or(tmp)) {
              for (uchar i = 0; i < num_reads; ++i) {
                  // Check if the i'th elements MSB is set, and if far enough
                  if (extract(i, tmp) && max_pos[i] < curr - read_len) {
                      //TODO add -(max_score/sub_score) term
                      insert(extract(i, S_curr[col]), i, sub_score);
                      sub_pos[i] = curr;
                      sub_count[i] = 0;
              }
          }
          }

          PROF_END("New Sub")
          PROF_BEGIN("Eq Sub")

          // Repeat sub score
          tmp = cmp_eq(S_curr[col], sub_score);
          if (reduce_or(tmp)) {
              for (uchar i = 0; i < num_reads; ++i) {
                  // Check if the i'th elements MSB is set
                  if (extract(i, tmp)) {
                      sub_count[i] += 1;
                      sub_pos[i] = curr;
              }
          }
      }

          PROF_END("Eq Sub")
      }

      /**
       * @brief
       * Extract the i'th element from a vector. No range checking is done.
       * @param i index of element
       * @param vec vector to extract from
       */
      __INLINE__
      NativeT extract(uint8_t i, const CellType<num_reads> &vec) {
          return ((NativeT *) &vec)[i];
      }

      /**
       * @brief
       * Insert into the i'th element from a vector. No range checking is done.
       * @param ins element to insert
       * @param i index of element
       * @param vec vector to insert in
       */
      __INLINE__
      void insert(NativeT ins, uint8_t i, const CellType<num_reads> &vec) {
          ((NativeT *) &vec)[i] = ins;
      }

      AlignmentGroup _ag;
      int read_len; /**< Maximum read length. */

      // Zero vector
      const CellType<num_reads> ZERO_CT = simdpp::splat(0);

      uint8_t
          _match = 2,       /**< Match score, is added */
          _mismatch = 2,    /**< mismatch penalty, is subtracted */
          _gap_open = 3,    /**< gap open penalty, subtracted */
          _gap_extend = 1;  /**< gap extension penalty, subtracted */

      /**
       * Each vector has an 'a' and a 'b' version. Through each row of the
       * matrix fill, their roles are swapped such that one becomes the previous
       * loops data, and the other is filled in.
       * S and D are padded 1 to provide a left column buffer.
       */
      VecType
          *Sa = nullptr,    /**< Matrix row */
          *Sb = nullptr,
          *Da = nullptr,    /**< Deletion vector */
          *Db = nullptr,
          *Ia = nullptr,    /**< Insertion vector */
          *Ib = nullptr;

      CellType<num_reads>
          *S_prev = nullptr,    /**< S_prev[n] => S(i-1, n) */
          *S_curr = nullptr,
          *D_prev = nullptr,    /**< D_prev[n] => D(i-1, n) */
          *D_curr = nullptr,
          *I_prev = nullptr,    /**< I_prev[r] => I(r, j-1) */
          *I_curr = nullptr,
          *swp_tmp;

      CellType<num_reads> tmp,  /**< temporary for use within functions */
          Ceq,  /**< Match score when read_base == ref_base */
          Cneq;
      /**< mismatch penalty */

      NativeT max_elem;
      uint32_t curr;

      // Optimal alignment info
      CellType<num_reads> max_score;
      uint32_t *max_pos;
      uint8_t *max_count;

      // Suboptimal alignment info
      CellType<num_reads> sub_score;
      uint32_t *sub_pos;
      uint8_t *sub_count;

      uint8_t *cor_flag;

      size_t _max_node_len;

  };

}

TEST_CASE ("Alignment") {

        SUBCASE("Graph Alignment") {
        Vargas::Graph::Node::_newID = 0;
        Vargas::Graph g;

        /**
        *     GGG
        *    /   \
        * AAA     TTTA
        *    \   /
        *     CCC(ref)
        */

        {
            Vargas::Graph::Node n;
            n.set_endpos(3);
            n.set_as_ref();
            std::vector<bool> a = {0, 1, 1};
            n.set_population(a);
            n.set_seq("AAA");
            g.add_node(n);
        }

        {
            Vargas::Graph::Node n;
            n.set_endpos(6);
            n.set_as_ref();
            std::vector<bool> a = {0, 0, 1};
            n.set_population(a);
            n.set_af(0.4);
            n.set_seq("CCC");
            g.add_node(n);
        }

        {
            Vargas::Graph::Node n;
            n.set_endpos(6);
            n.set_not_ref();
            std::vector<bool> a = {0, 1, 0};
            n.set_population(a);
            n.set_af(0.6);
            n.set_seq("GGG");
            g.add_node(n);
        }

        {
            Vargas::Graph::Node n;
            n.set_endpos(10);
            n.set_as_ref();
            std::vector<bool> a = {0, 1, 1};
            n.set_population(a);
            n.set_seq("TTTA");
            n.set_af(0.3);
            g.add_node(n);
        }

        g.add_edge(0, 1);
        g.add_edge(0, 2);
        g.add_edge(1, 3);
        g.add_edge(2, 3);
        g.set_popsize(3);

        std::vector<std::string> reads;
        reads.push_back("NNNCCTT");
        reads.push_back("NNNGGTT");
        reads.push_back("NNNAAGG");
        reads.push_back("NNNAACC");
        reads.push_back("NNAGGGT");
        reads.push_back("NNNNNGG");
        reads.push_back("AAATTTA");
        reads.push_back("AAAGCCC");

        Vargas::Aligner<> a(5, 7);

        Vargas::Aligner<>::Results aligns = a.align(reads, g.begin(), g.end());
            CHECK(aligns.best_score[0] == 8);
            CHECK(aligns.best_pos[0] == 8);

            CHECK(aligns.best_score[1] == 8);
            CHECK(aligns.best_pos[1] == 8);

            CHECK(aligns.best_score[2] == 8);
            CHECK(aligns.best_pos[2] == 5);

            CHECK(aligns.best_score[3] == 8);
            CHECK(aligns.best_pos[3] == 5);

            CHECK(aligns.best_score[4] == 10);
            CHECK(aligns.best_pos[4] == 7);

            CHECK(aligns.best_score[5] == 4);
            CHECK(aligns.best_pos[5] == 6);

            CHECK(aligns.best_score[6] == 8);
            CHECK(aligns.best_pos[6] == 10);

            CHECK(aligns.best_score[7] == 8);
            CHECK(aligns.best_pos[7] == 4);
    }
}
#endif //VARGAS_ALIGNMENT_H
