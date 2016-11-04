/**
 * @author Ravi Gaddipati
 * @date July 31, 2016
 * rgaddip1@jhu.edu
 *
 * @brief
 * Defines a set of subgraphs from a given reference and variant file.
 *
 * @file
 */

#ifndef VARGAS_GDEF_H
#define VARGAS_GDEF_H

#include <string>
#include <map>
#include <fstream>
#include "graph.h"
#include "varfile.h"

namespace vargas {

  /**
   * @brief
   * Create and manage a graph and specified subgraphs.
   * @details
   * Given a reference sequence and a variant call file, a GDEF file uniquely defines a set of
   * subgraphs deriving from the base graph. Subgraphs are generated by filtering the base graph
   * based on the number of samples in the graph. \n
   * Subgraphs are generated from parent graphs, with the top level base graph implied as 100%. ':' indicates
   * a child graph, '~' indicates the complement population. Multiple definitions are delimited with ';',
   * and all labels must be defined before being referenced. For example:
   * @code{.unparsed}
   * "a=10" # Choose 10 samples from the base graph
   * "b=5%" # Choose 5% of the samples in the base graph
   * "a:c=5" # Choose 5 of the 10 samples in 'a'
   * "~a:d=5" # Choose 5 samples from the complement population of 'a'
   * "a:~e=5" # Invalid, complement graphs are implicit and cannot be directly defined.
   * @endcode
   * @code{.unparsed}
   * "a=10t" # Use the first 10 haplotypes (VCF File)
   * @endcode
   *
   * The following labels are reserved:
   * - BASE: Full graph
   * - REF: Linear reference graph
   * - MAXAF: Linear graph with the maximum allele frequency
   *
   * \n
   * GDEF file format:
   * @code{.unparsed}
   * @gdef[:TYPE]
   * ref=REFERENCE,var=VCF,reg=REGION,nlen=NODE_LEN
   * label=POP_FILTER
   * ...
   * label=POP_FILTER
   * @endcode
   * Usage:
   * @code{.cpp}
   * #include <gdef.h>
   *
   * int main() {
   *    Vargas::GraphManager gm();
   *    gm.write("ref.fasta", "variants.vcf", "x:0-100", "a=50;a:b=10;~a:c=10%", 1000, "out.gdf");
   *
   *    gm.load("out.gdf");
   *    gm.base(); // shared_ptr to whole graph
   *    gm.subgraph("a"); // 'a' subgraph
   *    gm.subgraph("~a"); // complement of 'a' subgraph
   *    gm.subgraph("c"); // Invalid
   *    gm.subgraph("~a:c") // valid
   *    gm.subgraph("a:REF") // Only reference alleles from subgraph a
   *
   * }
   * @endcode
   * There are two versions to obtain graphs. The version prefixed with "make_" will create the graph
   * if it has not been made yet, gaurded with #pragma omp critical.
   * The const versions subgraph() and base() will return a nullptr if the
   * graph does not exist.
   */
  class GraphManager {
    public:

      GraphManager() {}

      /**
       * @brief
       * Open a GDEF file.
       * @param gdef_file file name
       * @throws std::invalid_argument Failed to open the input file.
       */
      GraphManager(std::string gdef_file);

      /**
       * @brief
       * Open a GDEF file from a stream.
       * @param in
       */
      GraphManager(std::istream &in) { open(in); }

      ~GraphManager() {
          close();
      }

      /**
       * @brief
       * Remove any graphs.
       */
      void close();

      /**
       * @brief
       * Open a GDEF file.
       * @param file_name
       * @param build_base If true, build the base graph.
       * @return true on success
       */
      bool open(std::string file_name,
                bool build_base = true);

      /**
       * @brief
       * Read a GDEF file from an input stream
       * @param in input stream
       * @param build_base If true, build the base graph.
       * @return true on success
       * @throws std::invalid_argument Invalid token or a duplicate definition
       * @throws std::range_error Filter length does not match the number of samples in the VCF file
       */
      bool open(std::istream &in,
                bool build_base = true);

      /**
       * @brief
       * Create a subgraph if it does not exist.
       * @param label subgraph name
       * @return shared_ptr to graph
       * @throws std::invalid_argument Label does not exist, or no base graph built
       */
      std::shared_ptr<const Graph> make_subgraph(std::string label);

      /**
       * @brief
       * Get a previously made graph.
       * @param label subgraph name
       * @return shared_ptr to graph
       * @throws std::out_of_range Graph does not exist
     */
      std::shared_ptr<const Graph> subgraph(std::string label) const;

      /**
       * @brief
       * Full graph
       * @return shared_ptr to base graph
       * @throw std::invalid_argument Base graph is not built.
       */
      std::shared_ptr<const Graph> base() const;

      /**
       * @brief
       * Reference graph
       * @return shared_ptr to base graph
       * @throw std::invalid_argument Base graph is not built.
       */
      std::shared_ptr<const Graph> make_ref(std::string const &label);

      /**
       * @brief
       * Max allele frequency graph
       * @return shared_ptr to base graph
       * @throw std::invalid_argument Base graph is not built.
       */
      std::shared_ptr<const Graph> make_maxaf(std::string const &label);

      /**
       * @brief
       * Get the population filter for a label
       * @param label subgraph label
       * @return Graph::Population to derive subgraph from base graph
       * @throws std::invalid_argument Label does not exist
       */
      Graph::Population filter(std::string label) const;

      /**
       * @brief
       * Remove local reference to a graph. Removing after creation of the graph
       * will free memory once all users destroy their shared_ptr's.
       * @param label graph name
       */
      void destroy(std::string label) {
          #pragma omp critical(gdef_destroy)
          {
              _subgraphs.erase(GDEF_BASEGRAPH + GDEF_SCOPE + label);
          }
      }

      /**
       * @brief
       * Remove all local references to subgraphs. The base graph is preserved.
       */
      void clear() {
          #pragma omp critical(gdef_destroy)
          {
              _subgraphs.clear();
          }
      }

      /**
       * @brief
       * Limit samples used from VCF.
       * @param filter list of sample names to include
       */
      void set_filter(std::string filter, bool invert = false);

      /**
       * @brief
       * Remove the sample filter.
       */
      void clear_filter() {
          _sample_filter = "-";
      }

      /**
       * @brief
       * Parse a defintion string a write a GDEF file. Also loads the generated file.
       * @param ref_file Reference file name
       * @param variant_file Variant file name
       * @param region Region in the format 'CHR:MIN-MAX'
       * @param defs subgraph definition string
       * @param node_len maximum graph node length.
       * @param out_file Output file name
       * @param build_base If true, build the base graph.
       * @return true on success
       * @throws std::invalid_argument Invalid output file
       */
      bool write(std::string ref_file,
                 std::string variant_file,
                 std::string region,
                 const std::string &defs,
                 int node_len,
                 std::string out_file,
                 bool build_base = true);

      /**
       * @brief
       * Parse a defintion string a write a GDEF file. Also loads the generated file.
       * @param ref_file Reference file name
       * @param variant_file Variant file name
       * @param region Region in the format 'CHR:MIN-MAX'
       * @param defs subgraph definition string
       * @param node_len maximum graph node length.
       * @param out output stream
       * @param build_base If true, build the base graph.
       * @param nsamps if unspecified the number of samples will be determined from the VCF file
       * @return true on success
       */
      bool write(std::string ref_file,
                 std::string variant_file,
                 std::string region,
                 std::string defs,
                 int node_len,
                 std::ostream &out,
                 bool build_base,
                 int nsamps = 0);

      /**
       * @brief
       * Parse a defintion string a write a GDEF file. Also loads the generated file.
       * @param ref_file Reference file name
       * @param vcf_file Variant file name
       * @param region Region in the format 'CHR:MIN-MAX'
       * @param defs subgraph definition string
       * @param max_node_len maximum graph node length.
       * @param out output stream
       * @return true on success
       */
      bool write_from_vcf(std::string ref_file,
                          std::string vcf_file,
                          std::string region,
                          std::string defs,
                          int max_node_len,
                          std::ostream &out) {
          return write(ref_file, vcf_file, region, defs, max_node_len, out, true);
      }

      /**
         * @brief
         * Parse a defintion string a write a GDEF file. Also loads the generated file.
         * @param ref_file Reference file name
         * @param vcf_file Variant file name
         * @param region Region in the format 'CHR:MIN-MAX'
         * @param defs subgraph definition string
         * @param max_node_len maximum graph node length.
         * @param out output file name
         * @return true on success
         */
      bool write_from_vcf(std::string ref_file,
                          std::string vcf_file,
                          std::string region,
                          std::string defs,
                          int max_node_len,
                          std::string out) {
          return write(ref_file, vcf_file, region, defs, max_node_len, out, true);
      }

      /**
     * @brief
     * Export the Population graph in DOT format
     * @param filename to export to
     * @param name of the graph
     * @throws std::invalid_argument if output file cannot be opened
     */
      void to_DOT(std::string filename,
                  std::string name) const {
          std::ofstream out(filename);
          if (!out.good()) throw std::invalid_argument("Error opening file: \"" + filename + "\"");
          out << to_DOT(name);
      }

      /**
       * @brief
       * Derive a graph of population dependencies
       * @param name Graph name
       * @return String of population graph in DOT format
       */
      std::string to_DOT(std::string name = "groups") const;

      /**
       * @brief
       * Get a vector of all labels that have a population.
       * @details
       * Each label is fully scoped.
       * @return vector of labels
       */
      std::vector<std::string> labels() const {
          std::vector<std::string> ret;
          for (const auto &pair : _subgraph_filters) {
              ret.push_back(pair.first);
          }
          return ret;
      }

      /**
       * @brief
       * Return the number of subgraphs defined.
       * @return Number of subgraph filters.
       */
      size_t size() const {
          return _subgraph_filters.size();
      }

      /*
       * @return Configured maximum node length
       */
      int node_len() const {
          return _node_len;
      }

      /**
       * @return Reference file fame
       */
      std::string reference() const {
          return _ref_file;
      }

      /**
       * @return Variant file name
       */
      std::string variants() const {
          return _variant_file;
      }

      std::string region() const {
          return _region;
      }

      const std::string GDEF_FILE_MARKER = "@gdef";
      const std::string GDEF_REF = "ref";
      const std::string GDEF_VAR = "var";
      const std::string GDEF_REGION = "reg";
      const std::string GDEF_NODELEN = "nlen";
      const std::string GDEF_BASEGRAPH = "BASE";
      const std::string GDEF_REFGRAPH = "REF";
      const std::string GDEF_MAXAFGRAPH = "MAXAF";
      const std::string GDEF_SAMPLE_FILTER = "FILTER";
      const std::string GDEF_NEGATE_FILTER = "INVERT";
      const char GDEF_NEGATE = '~';
      const char GDEF_SCOPE = ':';
      const char GDEF_ASSIGN = '=';
      const char GDEF_DELIM = ';';

    private:

      /**
       * When using a VCF file, the mapped population is the length of the # of genotypes.
       */
      std::unordered_map<std::string, Graph::Population> _subgraph_filters;
      std::unordered_map<std::string, std::shared_ptr<const Graph>> _subgraphs;

      std::string _ref_file, _variant_file, _region, _sample_filter = "-";
      bool _invert_filter;
      int _node_len;

  };

}

TEST_CASE ("Graph Manager") {
    using std::endl;
    std::string tmpfa = "tmp_tc.fa";
    {
        std::ofstream fao(tmpfa);
        fao
            << ">x" << endl
            << "CAAATAAGGCTTGGAAATTTTCTGGAGTTCTATTATATTCCAACTCTCTGGTTCCTGGTGCTATGTGTAACTAGTAATGG" << endl
            << "TAATGGATATGTTGGGCTTTTTTCTTTGATTTATTTGAAGTGACGTTTGACAATCTATCACTAGGGGTAATGTGGGGAAA" << endl
            << "TGGAAAGAATACAAGATTTGGAGCCAGACAAATCTGGGTTCAAATCCTCACTTTGCCACATATTAGCCATGTGACTTTGA" << endl
            << "ACAAGTTAGTTAATCTCTCTGAACTTCAGTTTAATTATCTCTAATATGGAGATGATACTACTGACAGCAGAGGTTTGCTG" << endl
            << "TGAAGATTAAATTAGGTGATGCTTGTAAAGCTCAGGGAATAGTGCCTGGCATAGAGGAAAGCCTCTGACAACTGGTAGTT" << endl
            << "ACTGTTATTTACTATGAATCCTCACCTTCCTTGACTTCTTGAAACATTTGGCTATTGACCTCTTTCCTCCTTGAGGCTCT" << endl
            << "TCTGGCTTTTCATTGTCAACACAGTCAACGCTCAATACAAGGGACATTAGGATTGGCAGTAGCTCAGAGATCTCTCTGCT" << endl
            << ">y" << endl
            << "GGAGCCAGACAAATCTGGGTTCAAATCCTGGAGCCAGACAAATCTGGGTTCAAATCCTGGAGCCAGACAAATCTGGGTTC" << endl;
    }

    std::srand(12345);

        SUBCASE("File Write Wrapper") {

            SUBCASE("VCF") {

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
                    << "##INFO=<ID=AC,Number=A,Type=Integer,Description=\"Alternate Allele count\">" << endl
                    << "##INFO=<ID=NS,Number=1,Type=Integer,Description=\"Num samples at site\">" << endl
                    << "##INFO=<ID=NA,Number=1,Type=Integer,Description=\"Num alt alleles\">" << endl
                    << "##INFO=<ID=LEN,Number=A,Type=Integer,Description=\"Length of each alt\">" << endl
                    << "##INFO=<ID=TYPE,Number=A,Type=String,Description=\"type of variant\">" << endl
                    << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\ts1\ts2" << endl
                    << "x\t9\t.\tG\tA,C,T\t99\t.\tAF=0.01,0.6,0.1;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t0|1\t2|3" << endl
                    << "x\t10\t.\tC\t<CN7>,<CN0>\t99\t.\tAF=0.01,0.01;AC=2;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|1\t2|1"
                    << endl
                    << "x\t14\t.\tG\t<DUP>,<BLAH>\t99\t.\tAF=0.01,0.1;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|0\t1|1"
                    << endl
                    << "y\t34\t.\tTATA\t<CN2>,<CN0>\t99\t.\tAF=0.01,0.1;AC=2;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|1\t2|1"
                    << endl
                    << "y\t39\t.\tT\t<CN0>\t99\t.\tAF=0.01;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|0\t0|1" << endl;
            }

            std::stringstream ss;
            vargas::GraphManager gm;
            gm.write_from_vcf(tmpfa,
                              tmpvcf,
                              "x:0-10",
                              "ingroup = 2;~ingroup:1_1=1;ingroup:1_2=1;top=2t",
                              100000,
                              ss);
            gm.open(ss);

                CHECK_THROWS(gm.filter("sdf"));

                CHECK(gm.filter("ingroup").count() == 2);
                CHECK((gm.filter("ingroup") && gm.filter("~ingroup")) == 0);
                CHECK(gm.filter("ingroup:1_2").count() == 1);
                CHECK((gm.filter("ingroup:1_2") && gm.filter("ingroup:~1_2")) == 0);
                CHECK((gm.filter("ingroup") && gm.filter("~ingroup:1_1")) == 0);
                CHECK(gm.filter("~ingroup:1_1").count() == 1);

                CHECK((gm.filter("ingroup") | gm.filter("~ingroup")).count() == 4);
                CHECK((gm.filter("ingroup:1_2") | gm.filter("ingroup:~1_2")) == gm.filter("ingroup"));

                CHECK(gm.filter("top").at(0) == 1);
                CHECK(gm.filter("top").at(1) == 1);
            for (size_t i = 2; i < gm.filter("top").size(); ++i) {
                    CHECK(gm.filter("top").at(i) == 0);
            }

            {
                auto in_graph = gm.make_subgraph("ingroup");
                    CHECK(in_graph.use_count() == 2);
                gm.destroy("ingroup");
                    CHECK(in_graph.use_count() == 1);
            }

            {
                auto in_graph = gm.make_subgraph("ingroup");
                    CHECK(in_graph.use_count() == 2);
                gm.clear();
                    CHECK(in_graph.use_count() == 1);
            }

            remove(tmpvcf.c_str());
        }

    }

    remove(tmpfa.c_str());
    remove((tmpfa + ".fai").c_str());

}
#endif //VARGAS_GDEF_H
