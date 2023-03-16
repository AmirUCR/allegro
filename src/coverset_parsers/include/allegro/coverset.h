#ifndef COVERSETCPP_H
#define COVERSETCPP_H

#include <string>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include <boost/dynamic_bitset.hpp>
#include "ortools/linear_solver/linear_solver.h"

namespace coversets
{
    class CoversetCPP
    {
    private:
        std::size_t num_trials;
        std::size_t num_species;
        std::size_t guide_length;
        std::size_t bits_required_to_store_seq;
        boost::dynamic_bitset<> all_species_bitset;

        // guide --> bitvector of species it hits
        std::unordered_map<boost::dynamic_bitset<>, std::pair<unsigned char, boost::dynamic_bitset<>>> coversets;

        std::string get_str();
        std::string decode_bitset(const std::string &encoded_str);
        std::string decode_bitset(boost::dynamic_bitset<> &encoded);

        std::unordered_set<std::string> randomized_rounding(
            std::vector<operations_research::MPVariable *> feasible_solutions);

    public:
        CoversetCPP(
            std::size_t num_species,
            std::size_t guide_length, 
            std::size_t num_trials);
            
        ~CoversetCPP();

        std::unordered_set<std::string> ortools_solver(); // to be removed
        void encode_and_save_dna(std::string &seq, unsigned char score, unsigned short species_id);
    };
}

#endif