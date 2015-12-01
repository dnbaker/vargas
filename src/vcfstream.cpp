/**
 * Ravi Gaddipati
 * November 23, 2015
 * rgaddip1@jhu.edu
 *
 * vmatch::vcfstream is a filtering wrapper for a VCF file.
 * Variant records are parsed and filtered according to the ingroup.
 *
 * vcfstream.cpp
 */


#include "../include/vcfstream.h"


void vargas::vcfstream::initVCF() {
  // Skip the comments
  do { std::getline(vcfFile, currentRecord); } while (currentRecord.substr(0, 2) == "##");
  if (currentRecord.at(0) != '#') throw std::invalid_argument("Expected header beginning with #");

  // Convert to uppercase
  transform(currentRecord.begin(), currentRecord.end(), currentRecord.begin(), ::toupper);
  currentRecord = currentRecord.substr(1); // Remove leading #
  std::vector<std::string> header = split(currentRecord, '\t');

  //Find locations
  fields.chrom = int32_t(std::find(header.begin(), header.end(), "CHROM") - header.begin());
  fields.pos = int32_t(std::find(header.begin(), header.end(), "POS") - header.begin());
  fields.id = int32_t(std::find(header.begin(), header.end(), "ID") - header.begin());
  fields.ref = int32_t(std::find(header.begin(), header.end(), "REF") - header.begin());
  fields.alt = int32_t(std::find(header.begin(), header.end(), "ALT") - header.begin());
  fields.qual = int32_t(std::find(header.begin(), header.end(), "QUAL") - header.begin());
  fields.filter = int32_t(std::find(header.begin(), header.end(), "FILTER") - header.begin());
  fields.info = int32_t(std::find(header.begin(), header.end(), "INFO") - header.begin());
  fields.format = int32_t(std::find(header.begin(), header.end(), "FORMAT") - header.begin());
  fields.indivOffset = fields.format + 1;
  fields.numIndivs = uint32_t((header.size() - fields.indivOffset) * 2);

  // Check required fields
  if (fields.pos < 0) throw std::invalid_argument("POS field not found.");
  if (fields.ref < 0) throw std::invalid_argument("ALT field not found.");
  if (fields.alt < 0) throw std::invalid_argument("REF field not found.");
  if (fields.info < 0) throw std::invalid_argument("INFO field not found.");
  if (fields.format < 0) throw std::invalid_argument("FORMAT field not found.");

  initilized = true;

  // Default ingroup is everyone
  createIngroup(100);

}


void vargas::vcfstream::splitCurrentRecord() {
  split(currentRecord, '\t', splitTemp);
  splitRecord.clear();
  for (int i = 0; i < splitTemp.size(); ++i) {
    if (i >= fields.indivOffset) {
      split(splitTemp[i], '|', splitDiploid);
      splitRecord.push_back(splitDiploid[0]);
      splitRecord.push_back(splitDiploid[1]);
    } else {
      splitRecord.push_back(splitTemp[i]);
    }
  }
}


bool vargas::vcfstream::getRecord(vargas::vcfrecord &vrecord) {
  if (!initilized) {
    throw std::invalid_argument("VCF file not provided.");
  }
  if (!getline(vcfFile, currentRecord)) {
    return false;
  }
  splitCurrentRecord();
  vrecord.pos = std::stoul(splitRecord[fields.pos]);
  vrecord.ref = splitRecord[fields.ref];
  vrecord.indivs.clear();
  vrecord.freqs.clear();
  split(splitRecord[fields.alt], ',', splitTemp);
  afSplit.clear();


  //TODO flag to check in case of no AF found
  // Get the list of AF's
  split(splitRecord[fields.info], ';', afSplit);
  // Find and split the AF entry
  for (auto e : afSplit) {
    if (e.substr(0, 3) == "AF=") {
      split(e.substr(3), ',', afSplit);
      break;
    }
  }


  validAF = true;
  if (afSplit.size() != splitTemp.size()) {
    std::cerr << "Alternate and AF field lengths do not match at pos " << splitRecord[fields.pos] << ".\n";
    validAF = false;
  }

  // Add the reference node
  altIndivs.clear();
  for (auto col : ingroup) {
    // 0 indicates it has the ref allele
    if (std::stoi(splitRecord[col]) == 0) altIndivs.push_back(col);
  }
  vrecord.indivs.emplace(vrecord.ref.c_str(), altIndivs);

  // Freq of the ref allele
  if (validAF) {
    // Get the reference frequency
    double_t sumaltAF = 0;
    for (auto a : afSplit) {
      sumaltAF += std::stof(a);
    }
    vrecord.freqs.emplace(vrecord.ref.c_str(), 1 - sumaltAF);
  }

  // For each alternate allele
  for (int i = 0; i < splitTemp.size(); i++) {
    altIndivs.clear();
    // For each indiv individual, check if it has the alt
    for (auto col : ingroup) {
      if (std::stoi(splitRecord[col]) == i + 1) altIndivs.push_back(col);
    }

    // Atleast one of the ingroup has the allele
    if (altIndivs.size() > 0) {
      // Check for copy number
      if (splitTemp[i].at(0) == '<') {
        if (splitTemp[i].substr(1, 3) == "CN") {
          int cn = std::stoi(splitTemp[i].substr(3, splitTemp[i].length() - 4));
          splitTemp[i] = (cn == 0) ? "-" : ""; // A dash is an empty allele
          for (int c = 0; c < cn; ++c) splitTemp[i] += vrecord.ref;
        }
        else {
          std::cerr << "Invalid allele found at pos " << vrecord.pos << ": " << splitTemp[i] << ". Ignoring record."
              << std::endl;
          return getRecord(vrecord);
        }
      }
      vrecord.indivs.emplace(splitTemp[i].c_str(), altIndivs);
      if (validAF) vrecord.freqs.emplace(splitTemp[i].c_str(), std::stof(afSplit[i]));
    }
  }
  return true;
}


void vargas::vcfstream::createIngroup(int32_t percent, long seed) {

  if (!initilized) throw std::invalid_argument("VCF file not provided.");

  if (seed != 0) {
    this->seed = seed;
    srand(seed);
  }
  ingroup.clear();

  if (percent == 100) {
    for (int i = fields.indivOffset; i < fields.numIndivs + fields.indivOffset; ++i) {
      ingroup.push_back(i);
    }
  }
  else if (percent == 0) {
    return;
  }
  else {
    for (int i = fields.indivOffset; i < fields.numIndivs + fields.indivOffset; ++i) {
      if (rand() % 10000 < percent * 100) ingroup.push_back(i);
    }
  }
  //TODO make sure guaranteed to be sorted
}


void vargas::vcfstream::createComplementIngroup(std::vector<uint32_t> &vec) {
  std::sort(vec.begin(), vec.end());
  ingroup.clear();
  for (int i = fields.indivOffset; i < fields.numIndivs + fields.indivOffset; ++i) {
    if (!std::binary_search(vec.begin(), vec.end(), i)) ingroup.push_back(i);
  }
  std::sort(ingroup.begin(), ingroup.end());
}


std::ostream &vargas::operator<<(std::ostream &os, const vargas::vcfrecord &vrec) {
  os << "POS: " << vrec.pos << std::endl;
  os << "REF: P(" << vrec.ref << ")=";
  if (vrec.freqs.size() != 0) {
    os << vrec.freqs.at(vrec.ref) << std::endl;
  } else {
    os << "-" << std::endl;
  }
  os << "ALTS: " << std::endl;
  for (auto &e : vrec.indivs) {
    os << "\tP(" << e.first << ")=";
    if (vrec.freqs.size() != 0) {
      os << vrec.freqs.at(e.first);
    } else {
      os << "-";
    }
    for (auto &i : e.second) {
      os << ", " << i;
    }
    os << std::endl;
  }
  return os;
}


void vargas::vcfstream::printIngroup(std::ostream &os) {
  os << "#";
  for (auto &e : ingroup) {
    os << e << ",";
  }
  os << std::endl;
}