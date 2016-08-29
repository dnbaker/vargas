/**
 * @author Ravi Gaddipati (rgaddip1@jhu.edu)
 * @date May 26, 2016
 *
 * @brief
 * Provides a C++ wrapper for htslib handling of VCF and BCF files.
 * @details
 * Both file types are handled transparently by htslib. The records
 * are parsed to substitute in copy number variations, and skip
 * records outside of a defined range. A subset of individuals can be
 * defined using create_ingroup.
 *
 * @file
 */

#ifndef VARGAS_VARFILE_H
#define VARGAS_VARFILE_H

#include <string>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <stdlib.h>
#include <iostream>
#include <time.h>
#include <set>
#include <unordered_map>
#include <map>
#include "dyn_bitset.h"
#include "utils.h"
#include "htslib/vcfutils.h"
#include "htslib/hts.h"
#include "doctest.h"

namespace Vargas {

  /**
   * @brief
   * Base class for files representing variants in a reference. Provides an interface
   * to iterate through variants, and encapsulates a reference region.
   */
  class VariantFile {
    public:
      VariantFile() : _chr(""), _min_pos(-1), _max_pos(-1) {}

      /**
       * @brief
       * Specify a region of the reference sequence.
       * @param chr Chromosome
       * @param min Minimum position, 0 indexed
       * @param max Max position, inclusive, 0 indexed
       */
      VariantFile(std::string const &chr,
                  int const min,
                  int const max) :
          _chr(chr), _min_pos(min), _max_pos(max) {}

      virtual ~VariantFile() {};

      typedef dyn_bitset<64> Population;

      /**
       * @brief
        * Set the minimum and maximum position, inclusive.
        * If max is <= 0, go until end.
        * @param chr contig ID
        * @param min Minimum position, 0 indexed
        * @param max Max position, inclusive, 0 indexed
     */
      void set_region(std::string chr,
                      int min,
                      int max) {
          _min_pos = min;
          _max_pos = max;
          _chr = chr;
      }

      /**
 * @brief
 * Parse a region string in the format: \n
 * CHR:XX,XXX-YY,YYY \n
 * commas are stripped, range is inclusive. 0 indexed.
 * If max is <= 0, go until end.
 * @param region region string
 */
      void set_region(std::string region);

      /**
 * @brief
 * Get the minimum position.
 * @return 0 indexed minimum pos
 */
      int region_lower() const {
          return _min_pos;
      }

      /**
       * @brief
       * Get the maximum position.
       * @return 0 indexed maximum position
       */
      int region_upper() const {
          return _max_pos;
      }

      /**
 * @brief
 * Current contig filter.
 * @return ID of current CHROM filter
 */
      std::string region_chr() const {
          return _chr;
      }

      /**
       * @brief
       * Load the next VCF record. All information is unpacked,
       * subject to sample set restrictions.
       * Upon initilization, the first record is loaded.
       * @return false on read error or if outside restriction range.
       */
      virtual bool next() = 0;

      /**
       * @return true if the opened file is good.
       */
      virtual bool good() = 0;

      /**
       * @return Current reference sequence
       */
      virtual std::string ref() const = 0;

      /**
       * @return Vector of alleles at the current position
       */
      virtual const std::vector<std::string> &alleles() const = 0;

      /**
       * @return Current position of variant
       */
      virtual int pos() const = 0;

      /**
       * @brief
       * Allele frequencies corresponding to alleles()
       * @return vector of freqs, 0-1
       */
      virtual const std::vector<float> &frequencies() const = 0;

      /**
       * @brief
       * Samples in the file. If samples are unknown, an empty vector is returned.
       * @return vector of sample names
       */
      virtual const std::vector<std::string> &samples() const = 0;

      /**
       * @brief
       * Number of samples in the file. For VCF, this is the number of individuals. for KSNP, this is
       * the number of SNPs.
       * @return
       */
      virtual int num_samples() const = 0;

      /**
       * @brief
       * Population that posseses the the given allele. If the variant source does not
       * have this information, {1, 1} is returned.
       * @return Population of the given allele at the current position.
       */
      virtual const Population &allele_pop(const std::string) const = 0;




    protected:
      std::string _chr;
      int _min_pos, _max_pos;
  };

/**
 * @brief
 * Provides an interface to a VCF/BCF file. Core processing
 * provided by htslib. \n
 * Usage: \n
 * @code{.cpp}
 * #include "varfile.h"
 *
 * Vargas::VCF vcf("variants.bcf");
 * std::vector<std::string> seqs = vcf.sequences(); // Names of CHROM's present
 *
 * vcf.set_region("22:0-10,000,000");
 * vcf.create_ingroup(50); // Randomly select 50% of individuals to include
 *
 * vcf.samples().size(); // 4 individuals
 *
 * // Allele with the maximum allele frequency for each VCF record
 * while(vcf.next()) {
 *  float max_af = 0;
 *  std::string max_allele;
 *  std::vector<float> &freqs = vcf.frequencies();
 *  for (size_t i = 0; i < freqs.size(); ++i) {
 *    if (f > max_af) {
 *     max_af = f;
 *     max_allele = vcf.alleles[i];
 *     }
 *   }
 *   std::cout << "Variant position:" << vcf.pos() << ", Ref:" << vcf.ref()
 *             << ", Max AF allele:" << max_allele << ", Freq:" << max_af
 *             << ", Population:" << vcf.allele_pop(max_allele).to_string();
 *
 *   // 4 samples/individuals = 8 genotypes
 *   // Variant position: 100 Ref: A, Max AF allele: G, Freq: 0.6, Population: 01100101
 * }
 *
 * @endcode
 */
  class VCF: public VariantFile {
    public:

      VCF() {}

      /**
       * @param file VCF/BCF File name
       */
      explicit VCF(std::string file) : _file_name(file) {
          _init();
      }

      /**
       * @param file VCF/BCF File name
       * @param chr Chromosome
       * @param min Min position, 0 indexed
       * @param max Max position, 0 indexed, inclusive
       */
      VCF(std::string file,
          std::string chr,
          int min,
          int max) :
          VariantFile(chr, min, max), _file_name(file) {
          _init();
      }

      ~VCF() {
          close();
      }

      /**
       * @brief
      * Get the specified format field from the record.
      * @tparam T Valid types are int, char, float.
      */
      template<typename T>
      class FormatField {
        public:
          /**
           * Get the specified field tag.
           * @param hdr VCF Header
           * @param rec current record
           * @param tag Field to get, e.g. "GT"
           */
          FormatField(bcf_hdr_t *hdr,
                      bcf1_t *rec,
                      std::string tag) : tag(tag) {
              if (!hdr || !rec || tag.length() == 0) throw std::invalid_argument("Invalid header, rec, or tag.");

              T *dst = nullptr;
              int n_arr = 0;

              int n = _get_vals(hdr, rec, tag, &dst, n_arr);
              if (n == -1) throw std::invalid_argument("No such tag in header: " + tag);
              else if (n == -2) throw std::invalid_argument("Header and tag type clash: " + tag);
              else if (n == -3) throw std::invalid_argument(tag + " does not exist in record.");

              for (int i = 0; i < n; ++i) {
                  values.push_back(dst[i]);
              }

              free(dst); // get_format_values allocates
          }

          std::vector<T> values;
          /**< Retrieved values. */
          std::string tag; /**< Type of FORMAT or INFO field. */

        private:
          // Change the parse type based on what kind of type we have
          inline int _get_vals(bcf_hdr_t *hdr,
                               bcf1_t *rec,
                               std::string tag,
                               int32_t **dst,
                               int &ndst) {
              return bcf_get_format_values(hdr, rec, tag.c_str(), (void **) dst, &ndst, BCF_HT_INT);
          }

          inline int _get_vals(bcf_hdr_t *hdr,
                               bcf1_t *rec,
                               std::string tag,
                               float **dst,
                               int &ndst) {
              return bcf_get_format_values(hdr, rec, tag.c_str(), (void **) dst, &ndst, BCF_HT_REAL);
          }

          inline int _get_vals(bcf_hdr_t *hdr,
                               bcf1_t *rec,
                               std::string tag,
                               char **dst,
                               int &ndst) {
              return bcf_get_format_values(hdr, rec, tag.c_str(), (void **) dst, &ndst, BCF_HT_STR);
          }
      };

      /**
       * @brief
       * Get the specified info field from the record.
       * @tparam T Valid types are int, char, float.
       */
      template<typename T>
      class InfoField {
        public:
          /**
           * @brief
           * Get the specified field.
           * @param hdr VCF Header
           * @param rec current record
           * @param tag Field to get, e.g. "GT"
           */
          InfoField(bcf_hdr_t *hdr,
                    bcf1_t *rec,
                    std::string tag) : tag(tag) {
              if (!hdr || !rec || tag.length() == 0) throw std::invalid_argument("Invalid header, rec, or tag.");

              T *dst = nullptr;
              int n_arr = 0;

              int n = _bcf_get_info_values(hdr, rec, tag, &dst, n_arr);
              if (n == -1) throw std::invalid_argument("No such tag in header: " + tag);
              else if (n == -2) throw std::invalid_argument("Header and tag type clash: " + tag);
              else if (n == -3) throw std::invalid_argument(tag + " does not exist in record.");

              for (int i = 0; i < n; ++i) {
                  values.push_back(dst[i]);
              }

              free(dst);
          }

          std::vector<T> values;
          /**< Retrieved values */
          std::string tag; /**< Type of FORMAT or INFO field. */

        private:
          // Change the parse type based on what kind of type we have
          inline int _bcf_get_info_values(bcf_hdr_t *hdr,
                                          bcf1_t *rec,
                                          std::string tag,
                                          int32_t **dst,
                                          int &ndst) {
              return bcf_get_info_values(hdr, rec, tag.c_str(), (void **) dst, &ndst, BCF_HT_INT);
          }

          inline int _bcf_get_info_values(bcf_hdr_t *hdr,
                                          bcf1_t *rec,
                                          std::string tag,
                                          float **dst,
                                          int &ndst) {
              return bcf_get_info_values(hdr, rec, tag.c_str(), (void **) dst, &ndst, BCF_HT_REAL);
          }

          inline int _bcf_get_info_values(bcf_hdr_t *hdr,
                                          bcf1_t *rec,
                                          std::string tag,
                                          char **dst,
                                          int &ndst) {
              return bcf_get_info_values(hdr, rec, tag.c_str(), (void **) dst, &ndst, BCF_HT_STR);
          }
      };

      /**
       * @brief
       * Open the specified VCF or BCF file and load the header.
       * @param file filename
       * @return -1 on file open error, -2 on header load error, 0 otherwise
       */
      int open(std::string file) {
          _file_name = file;
          return _init();
      }

      void close() {
          if (_bcf) bcf_close(_bcf);
          if (_header) bcf_hdr_destroy(_header);
          if (_curr_rec) bcf_destroy(_curr_rec);
          if (_ingroup_cstr) free(_ingroup_cstr);
          _bcf = nullptr;
          _header = nullptr;
          _curr_rec = nullptr;
          _ingroup_cstr = nullptr;
      }

      bool good() override {
          return _header && _bcf;
      }

      /**
       * @brief
       * Ingroup parameter on BCF reading. Empty string indicates none, "-" indicates all.
       * @return string of ingroup samples
       */
      std::string ingroup_str() const {
          if (!_ingroup_cstr) return "-";
          return std::string(_ingroup_cstr);
      }

      /**
       * @brief
       * Get a list of sequences in the VCF file.
       * @return vector of sequence names
       */
      std::vector<std::string> sequences() const;

      /**
       * @brief
       * num_samples() counts each haplotype as distinct. num_samples() = samples().size() * 2
       * @return Number of samples the VCF has. Each sample represents two genotypes.
       */
      int num_samples() const override {
          if (!_header) return -1;
          return bcf_hdr_nsamples(_header) * 2;
      }

      /**
       * @brief
       * Get a vector of sample names.
       * @return vector of samples
       */
      const std::vector<std::string> &samples() const override {
          return _samples;
      }

      /**
       * @brief
       * Load the next VCF record. All information is unpacked,
       * subject to sample set restrictions.
       * @return false on read error or if outside restriction range.
       */
      bool next() override;

      /**
       * @brief
       * Unpack only the shared information, and loads ref and allele info.
       */
      void unpack_shr() {
          bcf_unpack(_curr_rec, BCF_UN_SHR);
          _load_shared();
      }

      /**
       * @brief
       * Unpacks shared information as well as all sample information.
       * Subject to sample set restrictions.
       */
      void unpack_all() {
          bcf_unpack(_curr_rec, BCF_UN_ALL);
          _load_shared();
      }

      /**
       * @brief
       * Reference allele of the current record.
       * @return reference allele
       */
      std::string ref() const {
          return _alleles[0];
      }

      /**
       * @brief
       * List of all the alleles in the current record.
       * @details
       * The first is the reference. Allele copy number variant tags are converted,
       * whereas other tags are substituted for the reference.
       * @return vector of alleles
       */
      const std::vector<std::string> &alleles() const {
          return _alleles;
      }

      /**
       * @brief
       * 0 based position, i.e. the VCF pos - 1.
       * @return position.
       */
      int pos() const {
          return _curr_rec->pos;
      }

      /**
       * @brief
       * Get a list of alleles for all samples (subject to sample set restriction).
       * @details
       * Consecutive alleles represent phasing, e.g. all odd indexes are one phase,
       * all even indexes are the other. Call will unpack the full record.
       * Explicit copy number variations are replaced, other ambigous types are replaced
       * ambiguous.
       * A map is also built, mapping each allele to the subpopulation that has it.
       * @return Vector of alleles, ordered by sample.
       */
      const std::vector<std::string> &genotypes();

      /**
       * @brief
       * Get the allele frequencies of the ref and alt alleles.
       * The ref freq is computed with 1-sum(alt_frequencies).
       * @return const ref to vector of frequencies
       */
      const std::vector<float> &frequencies() const;

      /**
       * @brief
       * Get values of an arbitrary INFO tag.
       * @tparam T Field format
       * @param tag tag to retrieve values for
       * @return vector of values.
       */
      template<typename T>
      std::vector<T> info_tag(std::string tag) {
          return InfoField<T>(_header, _curr_rec, tag).values;
      }

      /**
       * @brief
      * Get values of an arbitrary FORMAT tag.
      * @tparam T Field format
       * @param tag tag to extract
      * @return vector of values.
      */
      template<typename T>
      std::vector<T> fmt_tag(std::string tag) {
          return FormatField<T>(_header, _curr_rec, tag).values;
      }

      /**
       * @brief
       * Return the population set that has the allele.
       * @details
       * The returned vector has the same size as number of genotypes (samples * 2).
       * When true, that individual/phase possed that allele.
       * @param allele allele to get the population of
       * @return Population of indviduals that have the allele
       */
      const Population &allele_pop(const std::string allele) const override {
          return _genotype_indivs.at(allele);
      }

      /**
       * @brief
       * Check if the file is properly loaded.
       * @return true if file is open and has a valid header.
       */
      bool good() const {
          return _header && _bcf;
      }

      /**
       * @brief
       * File name of VCF/BCF file.
       * @return file name
       */
      std::string file() const {
          return _file_name;
      }


      /**
       * @brief
       * Create a random subset of the samples.
       * @param percent of samples to keep.
       */
      void create_ingroup(int percent);

      /**
       * @brief
       * Include only the provided sample names in the Graph.
       * @param samples vector of sample names
       */
      void create_ingroup(const std::vector<std::string> &samples) {
          _ingroup = samples;
          _apply_ingroup_filter();
      }

      /**
       * @return string passed to htslib for ingroup filtering.
       */
      const std::vector<std::string> &ingroup() const {
          return _ingroup;
      }

    protected:

      /**
       * @brief
       * Open the provided file and load the header.
       * @return -1 on file open error, -2 on header read error.
       */
      int _init();

      /**
       * @brief
       * Loads the list of alleles for the current record.
       */
      void _load_shared();

      /**
       * @brief
       * Applies the contents of _ingroup to the header. The filter
       * impacts all following unpacks.
       */
      void _apply_ingroup_filter();


    private:
      std::string _file_name; // VCF/BCF file name

      htsFile *_bcf = nullptr;
      bcf_hdr_t *_header = nullptr;
      bcf1_t *_curr_rec = bcf_init();

      std::vector<std::string> _genotypes; // restricted to _ingroup
      std::unordered_map<std::string, Population> _genotype_indivs;
      std::vector<std::string> _alleles;
      std::vector<std::string> _samples;
      std::vector<std::string> _ingroup; // subset of _samples
      char *_ingroup_cstr = nullptr;

  };

  /**
   * @brief
   * Interface for a 1ksnp file. The given snps are included in the graph,
   * optionally filtered by another file.
   * The entire set of snps is loaded at init, to ensure we provide snps in the
   * correct order.
   */
  class KSNP: public VariantFile {

    public:
      /**
       * @param file_name ksnp file name
       * @param top_n only load the top n lines of file_name
       */
      KSNP(std::string const &file_name,
           int const top_n = 0) {
          open(file_name, top_n);
      }

      /**
       * @param input Input file stream
       * @param top_n only load the top n lines of input
       */
      KSNP(std::istream &input,
           int const top_n = 0) {
          open(input, top_n);
      }

      ~KSNP() {
          close();
      }

      /**
       * Represets the data in a line of a ksnp file
       */
      struct ksnp_record {
          ksnp_record() : count(0) {}

          /**
           * @param line Parse a ksnp line into a record
           */
          ksnp_record(std::string line);

          ksnp_record operator+(const ksnp_record &other) {
              if (count == 0) return *this = other;
              if (chr != other.chr || pos != other.pos) throw std::invalid_argument("SNPs are not at the same site.");
              if (ref != other.ref) throw std::invalid_argument("Inconsistent reference.");
              if (count != other.count) throw std::invalid_argument("Inconsistent count.");
              for (const auto &i : other.id) id.push_back(i);
              for (const auto &i : other.alt) alt.push_back(i);
              for (const auto &i : other.af) af.push_back(i);
              count += other.count;
              return *this;
          }

          ksnp_record operator+=(const ksnp_record &other) {
              return operator+(other);
          }

          std::string chr; /**< Chromosome */
          std::vector<std::string> id; /**< SNP ID */
          uint32_t pos; /**< SNP Position */
          std::string ref; /**< Reference base */
          std::vector<std::string> alt; /**< Alternate base */
          std::vector<float> af; /**< Allele frequency of alt */
          int count; /**< Number of variants at site. */
      };

      /**
       * @param file_name File name of ksnp file
       * @param top_n load top n lines
       */
      void open(std::string const &file_name,
                int const top_n = 0) {
          std::ifstream in(file_name);
          if (!in.good()) throw std::invalid_argument("Error opening ksnp file \"" + file_name + "\"");
          open(in, top_n);
          in.close();
      }

      /**
       * @param in Input stream of ksnp file
       * @param top_n Load top n lines
       */
      void open(std::istream &in,
                int const top_n = 0);

      /**
       * @brief
       * Clear loaded SNPs.
       */
      void close() {
          _snps.clear();
          _curr_iter = _snps.end();
          _curr_iter_idx = 0;
          _all_sample_ids.clear();
      }

      /**
       * Update current SNP. First SNP is loaded on file open.
       * @return true if SNP was updated.
       */
      bool next() override;

      bool good() override {
          return _curr_iter != _snps.end();
      }

      std::string ref() const override {
          return _curr_iter->second.ref;
      }

      const std::vector<std::string> &alleles() const override {
          return _curr_iter->second.alt;
      }

      int pos() const override {
          return _curr_iter->second.pos;
      }

      const std::vector<float> &frequencies() const override {
          return _curr_iter->second.af;
      }

      const Population &allele_pop(const std::string allele) const {
          static Population al_p;
          al_p = Population(num_samples(), false);
          if (allele == _curr_iter->second.ref) return al_p;
          auto al = std::find(_curr_iter->second.alt.begin(), _curr_iter->second.alt.end(), allele);
          if (al == _curr_iter->second.alt.end()) throw std::invalid_argument("Allele: \"" + allele + "\" not found.");
          al_p.set(_curr_iter_idx + (al - _curr_iter->second.alt.begin()));
          return al_p;
      }

      /**
       * @return Vector of SNP ID's
       */
      const std::vector<std::string> &samples() const {
          return _all_sample_ids;
      }

      int num_samples() const {
          return _all_sample_ids.size();
      }


    private:
      std::map<uint32_t, ksnp_record> _snps;
      std::map<uint32_t, ksnp_record>::iterator _curr_iter;
      std::vector<std::string> _all_sample_ids;
      size_t _curr_iter_idx;

  };

  TEST_CASE ("VCF File handler") {
      using std::endl;
      std::string tmpvcf = "tmp_tc.vcf";

      // Write temp VCF file
      {
          std::ofstream vcfo(tmpvcf);
          vcfo
              << "##fileformat=VCFv4.1" << endl
              << "##phasing=true" << endl
              << "##contig=<ID=x>" << endl
              << "##contig=<ID=y>" << endl
              << "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">" << endl
              << "##INFO=<ID=AF,Number=1,Type=Float,Description=\"Allele Freq\">" << endl
              << "##INFO=<ID=AC,Number=A,Type=Integer,Description=\"Alternete Allele count\">" << endl
              << "##INFO=<ID=NS,Number=1,Type=Integer,Description=\"Num samples at site\">" << endl
              << "##INFO=<ID=NA,Number=1,Type=Integer,Description=\"Num alt alleles\">" << endl
              << "##INFO=<ID=LEN,Number=A,Type=Integer,Description=\"Length of each alt\">" << endl
              << "##INFO=<ID=TYPE,Number=A,Type=String,Description=\"type of variant\">" << endl
              << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\ts1\ts2" << endl
              << "x\t9\t.\tG\tA,C,T\t99\t.\tAF=0.01,0.6,0.1;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t0|1\t2|3" << endl
              << "x\t10\t.\tC\t<CN2>,<CN0>\t99\t.\tAF=0.01,0.01;AC=2;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|1\t2|1" << endl
              << "x\t14\t.\tG\t<DUP>,<BLAH>\t99\t.\tAF=0.01,0.1;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|0\t1|1" << endl
              << "y\t34\t.\tTATA\t<CN2>,<CN0>\t99\t.\tAF=0.01,0.1;AC=2;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|1\t2|1" << endl
              << "y\t39\t.\tT\t<CN0>\t99\t.\tAF=0.01;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|0\t0|1" << endl;
      }

          SUBCASE("File write wrapper") {

              SUBCASE("Unfiltered") {
              VCF vcf(tmpvcf);
              vcf.next();
                  CHECK(vcf.num_samples() == 4);
                  CHECK(vcf.sequences().size() == 2);
                  CHECK(vcf.sequences()[0] == "x");
                  CHECK(vcf.sequences()[1] == "y");
                  REQUIRE(vcf.samples().size() == 2);
                  CHECK(vcf.samples()[0] == "s1");
                  CHECK(vcf.samples()[1] == "s2");

              // On load, first record is already loaded
                  REQUIRE(vcf.genotypes().size() == 4);
                  CHECK(vcf.genotypes()[0] == "G");
                  CHECK(vcf.genotypes()[1] == "A");
                  CHECK(vcf.genotypes()[2] == "C");
                  CHECK(vcf.genotypes()[3] == "T");
                  REQUIRE(vcf.alleles().size() == 4);
                  CHECK(vcf.alleles()[0] == "G");
                  CHECK(vcf.alleles()[1] == "A");
                  CHECK(vcf.alleles()[2] == "C");
                  CHECK(vcf.alleles()[3] == "T");
                  CHECK(vcf.ref() == "G");
                  CHECK(vcf.pos() == 8);

              // Copy number alleles
              vcf.next();
                  REQUIRE(vcf.genotypes().size() == 4);
                  CHECK(vcf.genotypes()[0] == "CC");
                  CHECK(vcf.genotypes()[1] == "CC");
                  CHECK(vcf.genotypes()[2] == "");
                  CHECK(vcf.genotypes()[3] == "CC");
                  REQUIRE(vcf.alleles().size() == 3);
                  CHECK(vcf.alleles()[0] == "C");
                  CHECK(vcf.alleles()[1] == "CC");
                  CHECK(vcf.alleles()[2] == "");
                  CHECK(vcf.ref() == "C");
                  CHECK(vcf.pos() == 9);

              // Invalid tags
              vcf.next();
                  REQUIRE(vcf.alleles().size() == 3);
                  CHECK(vcf.alleles()[0] == "G");
                  CHECK(vcf.alleles()[1] == "G");
                  CHECK(vcf.alleles()[2] == "G");
                  CHECK(vcf.ref() == "G");
                  CHECK(vcf.pos() == 13);

              // Next y contig should still load
              vcf.next();
                  CHECK(vcf.alleles()[0] == "TATA");
          }

              SUBCASE("CHROM Filtering") {
              VCF vcf;
              vcf.set_region("y:0-0");
              vcf.open(tmpvcf);

              vcf.next();
                  CHECK(vcf.ref() == "TATA");
              vcf.next();
                  CHECK(vcf.ref() == "T");
                  CHECK(vcf.next() == 0); // File end
          }

              SUBCASE("Region filtering") {
              VCF vcf;
              vcf.set_region("x:0-14");
              vcf.open(tmpvcf);

              vcf.next();
                  CHECK(vcf.ref() == "G");
              vcf.next();
                  CHECK(vcf.ref() == "C");
              vcf.next();
                  CHECK(vcf.ref() == "G");
                  CHECK(vcf.next() == 0); // Region end
          }

              SUBCASE("Ingroup generation") {
              VCF vcf;
              srand(12345);
              vcf.open(tmpvcf);
              vcf.create_ingroup(50);

                  CHECK(vcf.ingroup().size() == 1);
                  CHECK(vcf.ingroup()[0] == "s2");

              vcf.next();
                  REQUIRE(vcf.genotypes().size() == 2);
                  CHECK(vcf.genotypes()[0] == "C");
                  CHECK(vcf.genotypes()[1] == "T");

              vcf.next();
                  REQUIRE(vcf.genotypes().size() == 2);
                  CHECK(vcf.genotypes()[0] == "");
                  CHECK(vcf.genotypes()[1] == "CC");

              // Allele set should be complete, ingroup should reflect minimized set
                  CHECK(vcf.alleles().size() == 3);
                  CHECK(vcf.ingroup().size() == 1);
          }

              SUBCASE("Allele populations") {
              VCF vcf;
              vcf.open(tmpvcf);
              vcf.next();
              vcf.genotypes();

                  REQUIRE(vcf.allele_pop("G").size() == 4);
                  CHECK(vcf.allele_pop("G")[0]);
                  CHECK(!vcf.allele_pop("G")[1]);
                  CHECK(!vcf.allele_pop("G")[2]);
                  CHECK(!vcf.allele_pop("G")[3]);

                  REQUIRE(vcf.allele_pop("A").size() == 4);
                  CHECK(!vcf.allele_pop("A")[0]);
                  CHECK(vcf.allele_pop("A")[1]);
                  CHECK(!vcf.allele_pop("A")[2]);
                  CHECK(!vcf.allele_pop("A")[3]);

                  REQUIRE(vcf.allele_pop("C").size() == 4);
                  CHECK(!vcf.allele_pop("C")[0]);
                  CHECK(!vcf.allele_pop("C")[1]);
                  CHECK(vcf.allele_pop("C")[2]);
                  CHECK(!vcf.allele_pop("C")[3]);

                  REQUIRE(vcf.allele_pop("T").size() == 4);
                  CHECK(!vcf.allele_pop("T")[0]);
                  CHECK(!vcf.allele_pop("T")[1]);
                  CHECK(!vcf.allele_pop("T")[2]);
                  CHECK(vcf.allele_pop("T")[3]);

          }

              SUBCASE("Filtered allele populations") {
              VCF vcf;
              vcf.open(tmpvcf);
              vcf.create_ingroup({"s1"});
              vcf.next();
              vcf.genotypes();

                  REQUIRE(vcf.allele_pop("G").size() == 2);
                  CHECK(vcf.allele_pop("G")[0]);
                  CHECK(!vcf.allele_pop("G")[1]);

                  REQUIRE(vcf.allele_pop("A").size() == 2);
                  CHECK(!vcf.allele_pop("A")[0]);
                  CHECK(vcf.allele_pop("A")[1]);

                  REQUIRE(vcf.allele_pop("C").size() == 2);
                  CHECK(!vcf.allele_pop("C")[0]);
                  CHECK(!vcf.allele_pop("C")[1]);

                  REQUIRE(vcf.allele_pop("T").size() == 2);
                  CHECK(!vcf.allele_pop("T")[0]);
                  CHECK(!vcf.allele_pop("T")[1]);
          }

              SUBCASE("Allele frequencies") {
              VCF vcf;
              vcf.open(tmpvcf);
              vcf.next();

              auto af = vcf.frequencies();
                  REQUIRE(af.size() == 4);
                  CHECK(af[0] > 0.289f); // af[0] should be 0.29
                  CHECK(af[0] < 0.291f);
                  CHECK(af[1] == 0.01f);
                  CHECK(af[2] == 0.6f);
                  CHECK(af[3] == 0.1f);
          }

      }

      remove(tmpvcf.c_str());
  }

  TEST_CASE ("KSNP File Handler") {
      using std::endl;
      std::string tmpksnp = "tmp_tc.ksnp";

      // Write temp KSNP file
      {
          std::ofstream ko(tmpksnp);
          ko
              << "22      10        T       G       0.125   99      1       rs79667666\n"
              << "22      15        T       G       0.125   99      2       rs577223570\n"
              << "22      20        A       G       0.125   99      1       rs560440826\n"
              << "22      25        A       A       0.125   99      1       rs542836275\n"
              << "22      30        T       A       0.125   99      1       rs2899171\n"
              << "22      35        A       C       0.375   99      1       rs531500837\n"
              << "22      40        T       G       0.625   99      1       rs60683537\n"
              << "22      12        G       T       0.125   99      1       rs527731052\n"
              << "22      13        G       T       0.125   99      1       rs536519999\n"
              << "22      14        G       G       0.125   99      1       rs138497313\n"
              << "22      15        T       C       0.250   99      2       rs569928668\n"
              << "22      16        G       A       0.125   99      1       rs562028339\n" // Line 12
              << "22      17        A       A       0.625   99      1       rs557479846\n"
              << "22      18        A       G       0.125   99      1       rs9609408\n";
      }

          SUBCASE("File write wrapper") {

              SUBCASE("Basic file") {
              KSNP ksnp(tmpksnp, 12);
                  REQUIRE(ksnp.good() == true);

                  CHECK(ksnp.ref() == "T");
                  REQUIRE(ksnp.alleles().size() == 1);
                  CHECK(ksnp.alleles()[0] == "G");
                  REQUIRE(ksnp.frequencies().size() == 1);
                  CHECK(ksnp.frequencies()[0] == 0.125);
                  CHECK(ksnp.pos() == 10);
                  REQUIRE(ksnp.allele_pop("G").size() == 12);
                  CHECK(ksnp.allele_pop("G")[0] == 1);
                  CHECK(ksnp.allele_pop("G")[1] == 0);
                  CHECK_THROWS(ksnp.allele_pop("sdfsd"));

                  CHECK(ksnp.next() == true);

                  CHECK(ksnp.ref() == "G");
                  REQUIRE(ksnp.alleles().size() == 1);
                  CHECK(ksnp.alleles()[0] == "T");
                  REQUIRE(ksnp.frequencies().size() == 1);
                  CHECK(ksnp.frequencies()[0] == 0.125);
                  CHECK(ksnp.pos() == 12);

                  CHECK(ksnp.next() == true);

                  CHECK(ksnp.ref() == "G");
                  REQUIRE(ksnp.alleles().size() == 1);
                  CHECK(ksnp.alleles()[0] == "T");
                  REQUIRE(ksnp.frequencies().size() == 1);
                  CHECK(ksnp.frequencies()[0] == 0.125);
                  CHECK(ksnp.pos() == 13);

                  CHECK(ksnp.next() == true);

                  CHECK(ksnp.ref() == "G");
                  REQUIRE(ksnp.alleles().size() == 1);
                  CHECK(ksnp.alleles()[0] == "G");
                  REQUIRE(ksnp.frequencies().size() == 1);
                  CHECK(ksnp.frequencies()[0] == 0.125);
                  CHECK(ksnp.pos() == 14);

                  CHECK(ksnp.next() == true);

                  CHECK(ksnp.ref() == "T");
                  REQUIRE(ksnp.alleles().size() == 2);
                  CHECK(ksnp.alleles()[0] == "G");
                  CHECK(ksnp.alleles()[1] == "C");
                  REQUIRE(ksnp.frequencies().size() == 2);
                  CHECK(ksnp.frequencies()[0] == 0.125);
                  CHECK(ksnp.frequencies()[1] == 0.250);
                  CHECK(ksnp.pos() == 15);

                  CHECK(ksnp.next() == true);
                  CHECK(ksnp.pos() == 16);
                  CHECK(ksnp.next() == true);
                  CHECK(ksnp.pos() == 20);
                  CHECK(ksnp.next() == true);
                  CHECK(ksnp.pos() == 25);
                  CHECK(ksnp.next() == true);
                  CHECK(ksnp.pos() == 30);
                  CHECK(ksnp.next() == true);
                  CHECK(ksnp.pos() == 35);
                  CHECK(ksnp.next() == true);
                  CHECK(ksnp.pos() == 40);

                  CHECK_FALSE(ksnp.next());
          }

              SUBCASE("Bad file") {
              std::stringstream ss;
              ss
                  << "22  10  T  G  0.125  99  1  rs79667666\n"
                  << "22  10  A  G  0.125  99  2  rs577223570\n";

              try {
                  KSNP ksnp(ss);
                  std::cerr << "Exception expected.\n";
                      CHECK_FALSE(1);
              } catch (std::exception &e) {}
          }

      }

      remove(tmpksnp.c_str());
  }
}

#endif //VARGAS_VARFILE_H
