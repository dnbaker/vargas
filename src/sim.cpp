/**
 * @author Ravi Gaddipati
 * @date June 26, 2016
 * rgaddip1@jhu.edu
 *
 * @brief
 * Simulates random reads from a graph, returning reads that follow a specified Sim::Profile.
 *
 * @file
 */

#include "sim.h"

bool Vargas::Sim::_update_read() {

    uint32_t curr_indiv;

    // Pick an individual
    do {
        curr_indiv = rand() % _graph.pop_size();
    } while (!_graph.filter()[curr_indiv]);


    // Pick random weighted node and position within the node
    uint32_t curr_node;
    do {
        curr_node = _random_node_id();
    } while (!_nodes.at(curr_node)->belongs(curr_indiv));
    unsigned curr_pos = rand() % _nodes.at(curr_node)->length();


    int var_bases = 0;
    int var_nodes = 0;
    std::string read_str = "";

    while (true) {
        // Extract len subseq
        size_t len = _prof.len - read_str.length();
        if (len > _nodes.at(curr_node)->length() - curr_pos) len = _nodes.at(curr_node)->length() - curr_pos;
        read_str += _nodes.at(curr_node)->seq_str().substr(curr_pos, len);

        if (!_nodes.at(curr_node)->is_ref()) {
            ++var_nodes;
            var_bases += len;
        }

        assert(read_str.length() <= _prof.len);
        if (read_str.length() == _prof.len) break; // Done

        // Pick random next node.
        if (_next.find(curr_node) == _next.end()) return false; // End of graph

        std::vector<uint32_t> valid_next;
        for (const uint32_t n : _next.at(curr_node)) {
            if (_nodes.at(n)->belongs(curr_indiv)) valid_next.push_back(n);
        }
        if (valid_next.size() == 0) return false;
        curr_node = valid_next[rand() % valid_next.size()];
        curr_pos = 0;
    }

    if (std::count(read_str.begin(), read_str.end(), 'N') >= _prof.len / 2) return false;
    if (_prof.var_nodes >= 0 && var_nodes != _prof.var_nodes) return false;
    if (_prof.var_bases >= 0 && var_bases != _prof.var_bases) return false;

    // Introduce errors
    int sub_err = 0;
    int indel_err = 0;
    std::string read_mut = "";

    // Rate based errors
    if (_prof.rand) {
        for (size_t i = 0; i < read_str.length(); ++i) {
            char m = read_str[i];
            // Mutation error
            if (rand() % 10000 < 10000 * _prof.mut) {
                do {
                    m = rand_base();
                } while (m == read_str[i]);
                ++sub_err;
            }

            // Insertion
            else if (rand() % 10000 < 5000 * _prof.indel) {
                read_mut += rand_base();
                ++indel_err;
            }

            // Deletion (if we don't enter)
            else if (rand() % 10000 > 5000 * _prof.indel) {
                read_mut += m;
                ++indel_err;
            }
        }
    }
    else {
        // Fixed number of errors
        sub_err = (int) std::round(_prof.mut);
        indel_err = (int) std::round(_prof.indel);
        std::set<size_t> mut_sites;
        std::set<size_t> indel_sites;
        read_mut = read_str;
        {
            size_t loc;
            for (int j = 0; j < sub_err; ++j) {
                do {
                    loc = rand() % read_mut.length();
                } while (mut_sites.count(loc));
                mut_sites.insert(loc);
            }
            for (int i = 0; i < indel_err; ++i) {
                do {
                    loc = rand() % read_mut.length();
                } while (indel_sites.count(loc) || mut_sites.count(loc));
                indel_sites.insert(loc);
            }
        }

        for (size_t m : mut_sites) {
            do {
                read_mut[m] = rand_base();
            } while (read_mut[m] == read_str[m]);
        }

        for (size_t i : indel_sites) {
            if (rand() % 2) {
                // Insertion
                read_mut.insert(i, 1, rand_base());
            } else {
                // Deletion
                read_mut.erase(i, 1);
            }
        }
    }

    _read.flag.unmapped = false;
    _read.flag.aligned = true;

    _read.seq = read_mut;
    _read.aux.set(SIM_SAM_INDIV_TAG, (int) curr_indiv);
    _read.aux.set(SIM_SAM_INDEL_ERR_TAG, indel_err);
    _read.aux.set(SIM_SAM_VAR_BASE_TAG, var_bases);
    _read.aux.set(SIM_SAM_VAR_NODES_TAG, var_nodes);
    _read.aux.set(SIM_SAM_SUB_ERR_TAG, sub_err);

    // +1 from length being 1 indexed but end() being zero indexed, +1 since POS is 1 indexed.
    _read.pos = _nodes.at(curr_node)->end() - _nodes.at(curr_node)->length() + 2 + curr_pos - _prof.len;

    _read.aux.set(SIM_SAM_READ_ORIG_TAG, read_str);

    return true;
}