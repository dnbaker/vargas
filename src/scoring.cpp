/**
 * Ravi Gaddipati
 * Jan 10, 2016
 * rgaddip1@jhu.edu
 *
 * @brief
 * Program CL parsing and scoring structs.
 *
 * @copyright
 * Distributed under the MIT Software License.
 * See accompanying LICENSE or https://opensource.org/licenses/MIT
 *
 * @file
 */

#include "scoring.h"

void vargas::ScoreProfile::from_string(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), isspace), s.end());
    auto sp = rg::split(s, ",");
    for (const auto &tk : sp) {
        auto pr = rg::split(tk, "=");
        if (pr.size() != 2) throw std::invalid_argument("Invalid token: " + tk);
        const auto &k = pr[0];
        const auto &v = pr[1];
        if (k == "M") match = std::stoi(v);
        else if (k == "MM") mismatch = std::stoi(v);
        else if (k == "GOD") read_gopen = std::stoi(v);
        else if (k == "GED") read_gext = std::stoi(v);
        else if (k == "GOF") ref_gopen = std::stoi(v);
        else if (k == "GEF") ref_gext = std::stoi(v);
        else if (k == "ETE") end_to_end = v == "1";
        else if (k == "TOL") tol = std::stoi(v);
        else if (k == "AMB") ambig = std::stoi(v);
    }
}

std::string vargas::ScoreProfile::to_string() const {
    std::ostringstream ss;
    ss << "M=" << (int) match
       << ",MM=" << (int) mismatch
       << ",GOD=" << (int) read_gopen
       << ",GED=" << (int) read_gext
       << ",GOF=" << (int) ref_gopen
       << ",GEF=" << (int) ref_gext
       << ",AMB=" << (int) ambig
       << ",ETE=" << end_to_end
       << ",TOL=" << tol;
    return ss.str();
}

void vargas::Results::resize(size_t size) {
    max_pos.resize(size);
    sub_pos.resize(size);
    max_count.resize(size);
    sub_count.resize(size);
    max_score.resize(size);
    sub_score.resize(size);
    correct.resize(size);
    target_score.resize(size);
    max_strand.resize(size);
    sub_strand.resize(size);
}

void vargas::Results::finalize(const std::vector<target_t> &targets) {
    assert(targets.size() == correct.size());
    std::fill(correct.begin(), correct.end(), 0);
    for (unsigned i = 0; i < targets.size(); ++i) {
        if (targets.at(i).second == 0) continue;
        else if (targets.at(i).second >= max_pos[i] - profile.tol &&
                 targets.at(i).second <= max_pos[i] + profile.tol &&
                 targets.at(i).first == max_strand[i]) {
            correct[i] = 1;
        }
        else if (targets.at(i).second >= sub_pos[i] - profile.tol &&
                 targets.at(i).second <= sub_pos[i] + profile.tol &&
                 targets.at(i).first == sub_strand[i]) {
            correct[i] = 2;
        }
    }
}

std::vector<std::string> vargas::tokenize_cl(std::string cl) {
    std::replace_if(cl.begin(), cl.end(), isspace, ' ');
    cl.erase(std::unique(cl.begin(), cl.end(), [](char a, char b) { return a == b && a == '-'; }), cl.end());
    return rg::split(cl, " =");
}

vargas::ScoreProfile vargas::bwt2(const std::string &cl) {
    auto sp = tokenize_cl(cl);

    if (std::find(sp.begin(), sp.end(), "-U") == sp.end()) {
        throw std::invalid_argument("Bowtie2/HISAT: Unpaired read alignment expected.");
    }

    ScoreProfile ret;

    auto f = std::find(sp.begin(), sp.end(), "-local");
    if (f != sp.end()) ret.end_to_end = false;
    else ret.end_to_end = true;

    f = std::find(sp.begin(), sp.end(), "-np");
    if (f != sp.end()) ret.ambig = std::stoi(*++f);
    else ret.ambig = 1;

    ret.match = ret.end_to_end ? 0 : 2;
    if (!ret.end_to_end) {
        // Match always 0 in end to end
        f = std::find(sp.begin(), sp.end(), "-ma");
        if (f != sp.end()) ret.match = std::stoi(*++f);
    }

    f = std::find(sp.begin(), sp.end(), "-mp");
    if (f != sp.end()) ret.mismatch = std::stoi(*++f);
    else ret.mismatch = 6;

    f = std::find(sp.begin(), sp.end(), "-rfg");
    if (f != sp.end()) {
        auto sp = rg::split(*++f, ",");
        assert(sp.size() == 2);
        ret.ref_gopen = std::stoi(sp[0]);
        ret.ref_gext = std::stoi(sp[1]);
    } else {
        ret.ref_gopen = 5;
        ret.ref_gext = 3;
    }

    f = std::find(sp.begin(), sp.end(), "-rdg");
    if (f != sp.end()) {
        auto sp = rg::split(*++f, ",");
        assert(sp.size() == 2);
        ret.read_gopen = std::stoi(sp[0]);
        ret.read_gext = std::stoi(sp[1]);
    } else {
        ret.read_gopen = 5;
        ret.read_gext = 3;
    }

    return ret;
}

vargas::ScoreProfile vargas::bwa_mem(const std::string &cl) {
    auto sp = tokenize_cl(cl);

    ScoreProfile ret;
    ret.end_to_end = false;
    ret.ambig = 0;

    auto f = std::find(sp.begin(), sp.end(), "-A");
    if (f != sp.end()) ret.match = std::stoi(*++f);
    else ret.match = 1;

    f = std::find(sp.begin(), sp.end(), "-B");
    if (f != sp.end()) ret.mismatch = std::stoi(*++f);
    else ret.mismatch = 4;

    f = std::find(sp.begin(), sp.end(), "-O");
    if (f != sp.end()) ret.read_gopen = std::stoi(*++f);
    else ret.read_gopen = 6;

    f = std::find(sp.begin(), sp.end(), "-E");
    if (f != sp.end()) ret.read_gext = std::stoi(*++f);
    ret.read_gext = 1;

    ret.ref_gopen = ret.read_gopen;
    ret.ref_gext = ret.read_gext;

    return ret;

}

vargas::ScoreProfile vargas::program_profile(const std::string &cl) {
    if (cl.find("bowtie2") != std::string::npos) return bwt2(cl);
    else if (cl.find("hisat2") != std::string::npos) return bwt2(cl);
    else if (cl.find("bwa mem") != std::string::npos) return bwa_mem(cl);
    else throw std::invalid_argument("Unsupported program ID: " + cl);
}
