#include "allegro/coverset2.h"

namespace coversets
{
    CoversetCPP::CoversetCPP(
        std::size_t num_containers,
        std::size_t guide_length,
        std::size_t num_trials,
        std::string output_directory)
    {
        this->num_containers = num_containers; // A container may be a gene, species, or chromosome.
        this->guide_length = guide_length;     // Twenty (20) for cas9.
        this->num_trials = num_trials;         // The randomized rounding algorithm will restart this many times
                                               // to find the smallest subset of the feasible solutions.
        this->output_directory = output_directory;
        this->bits_required_to_store_seq = guide_length * 2; // Each nucleotide A/C/T/G in a guide
                                                             // can be represented by 2 bits.

        this->all_containers_bitset = boost::dynamic_bitset<>(num_containers);
    }

    CoversetCPP::~CoversetCPP() {}

    int CoversetCPP::encode_and_save_dna(
        std::string seq,
        std::size_t score,
        std::size_t container_id)
    {
        std::string encoded_str("");

        for (auto base : seq)
        {
            switch (base)
            {
            case 'A':
                encoded_str += sA_SHIFT;
                break;
            case 'C':
                encoded_str += sC_SHIFT;
                break;
            case 'G':
                encoded_str += sG_SHIFT;
                break;
            case 'T':
                encoded_str += sT_SHIFT;
                break;
            default:
                std::cerr << "Invalid DNA character: '" << base << "' encountered in " << seq << " in container id " << container_id << ". Skipping sequence.\n";
                return 1;
            }
        }

        // This is the encoded bitset for a guide's sequence
        boost::dynamic_bitset<> encoded_bitset = boost::dynamic_bitset<>(encoded_str);

        // If the sequence already exists in the memory
        if (coversets.find(encoded_bitset) != coversets.end())
        {
            // Set the appropriate bit to indicate that this new container is hit by this guide.
            this->coversets[encoded_bitset].second.set(container_id);

            // todo: Update the average score of this guide
            // need to keep track of how many times this guide has been seen: n
            // new_average = old_average + (new_item - old_average) / (n + 1)
        }
        else
        {
            // If it is the first time we encounter this sequence
            // Create a bitset with width equal to the number of containers
            // For example, if we have 10 species, we will have 0000000000
            boost::dynamic_bitset<> bitset(this->num_containers);
            bitset.set(container_id); // Set the appropriate bit for this container to 1

            // Indicate that this guide (its encoded bitset) has a pair<score, and the container it targets>
            this->coversets[encoded_bitset] = std::pair<char, boost::dynamic_bitset<>>(score, bitset);
        }

        // Keep a record of which species should be hit. We compare against this later in randomized_rounding
        //  to determine if a set of feasible solutions hits all the required species or not.
        // We do this record keeping here instead of in the constructor because some species
        //  may not contain any guides and should be excluded from consideration (so we don't set those bits).
        this->all_containers_bitset.set(container_id);

        return 0;
    }

    std::vector<std::pair<std::string, std::string>> CoversetCPP::ortools_solver(
        std::size_t container_cut_multiplicity,
        std::size_t mp_threshold,
        std::size_t beta)
    {
        std::unordered_map<boost::dynamic_bitset<>, std::pair<boost::dynamic_bitset<>, char>> species_already_hit_by_unique_guide;
        std::unordered_map<boost::dynamic_bitset<>, std::unordered_set<boost::dynamic_bitset<>>> hit_species;
        // Create the linear solver with the GLOP backend.
        std::unique_ptr<operations_research::MPSolver> solver(operations_research::MPSolver::CreateSolver("GLOP"));
        const double infinity = solver->infinity(); // Used for constraints to denote >= 1 and <= 1

        // --------------------------------------------------
        // -------------- VARIABLE CREATION -----------------
        // --------------------------------------------------
        operations_research::MPObjective *const objective = solver->MutableObjective();

        std::unordered_map<boost::dynamic_bitset<>, operations_research::MPVariable *> map_seq_to_vars;

        auto it = this->coversets.begin();
        while (it != this->coversets.end())
        {
            unsigned char score = it->second.first;

            // Remove redundant guides. Either predicted inefficient by the scorer,
            //  or marked as not needed above.
            if (score <= 0)
            {
                it = this->coversets.erase(it);
                continue;
            }

            boost::dynamic_bitset<> guide_seq_bitset = it->first;
            boost::dynamic_bitset<> species_bitset = it->second.second;

            // Find the first container hit by this guide and while there are containers left to process...
            size_t set_bit_index = species_bitset.find_first();
            while (set_bit_index != boost::dynamic_bitset<>::npos)
            {
                // Create a onehot bitvector for this one species specifically.
                // For three species, this would be:
                //  first iteration: bit vector 001 for the first species,
                //  second iteration: 010 for the second, and then third iteration 100 for the third.
                boost::dynamic_bitset<> species_onehot(species_bitset.size());
                species_onehot.set(set_bit_index);

                // Indicate that this species is hit by this guide.
                // For example, 001: 00110011... when species 1 is hit by this ATAT... guide
                // and on the next iteration, if ATAT hits species 2 as well:
                //  010: 00110011...
                hit_species[species_onehot].insert(guide_seq_bitset);

                // Find the next species hit by this guide for the next iteration.
                // Returns boost::dynamic_bitset<>::npos if no other bits are set.
                set_bit_index = species_bitset.find_next(set_bit_index);
            }

            std::string buffer;
            boost::to_string(guide_seq_bitset, buffer);
            operations_research::MPVariable *const var = solver->MakeNumVar(0.0, 1, buffer);

            map_seq_to_vars[guide_seq_bitset] = var;

            objective->SetCoefficient(var, score); // do 100 - old_score to flip the scores

            it++;
        }

        LOG(INFO) << "Number of variables (guides) = " << solver->NumVariables();
        this->log_buffer << "Number of variables (guides) = " << solver->NumVariables() << std::endl;
        // --------------------------------------------------

        // --------------------------------------------------
        // ------------- CONSTRAINT CREATION ----------------
        // --------------------------------------------------
        for (auto i : hit_species)
        {
            std::vector<operations_research::MPVariable *> vars_for_this_species;
            std::unordered_set<boost::dynamic_bitset<>> seq_bitsets_set = i.second;

            for (auto j : seq_bitsets_set)
            {
                vars_for_this_species.push_back(map_seq_to_vars[j]);
            }

            // All species must be covered by at least container_cut_multiplicity guide(s)
            operations_research::MPConstraint *const constraint = solver->MakeRowConstraint(container_cut_multiplicity, infinity);
            for (auto k : vars_for_this_species)
            {
                constraint->SetCoefficient(k, 1);
            }
        }

        hit_species.clear(); // Mark memory as free

        LOG(INFO) << "Number of constraints (genes) = " << solver->NumConstraints();
        this->log_buffer << "Number of constraints (genes) = " << solver->NumConstraints() << std::endl;
        // --------------------------------------------------

        // Set the objective and solve.
        LOG(INFO) << "Cut multiplicity: " << container_cut_multiplicity << " - Minimizing the the set size. Disregarding scores." << std::endl;
        this->log_buffer << "Cut multiplicity: " << container_cut_multiplicity << " - Minimizing the the set size. Disregarding scores." << std::endl;

        objective->SetMinimization();

        const operations_research::MPSolver::ResultStatus result_status = solver->Solve();

        // Check that the problem has an optimal solution.
        std::cout << "Status: " << result_status << std::endl;
        this->log_buffer << "Status: " << result_status << std::endl;
        if (result_status != operations_research::MPSolver::OPTIMAL)
        {
            LOG(FATAL) << "The problem does not have an optimal solution!";
            this->log_buffer << "The problem does not have an optimal solution!" << std::endl;
        }

        // Save the feasible variables.
        // A feasible variable is any variable with a solution value greater than 0.
        // It has a chance to be in the final solution.
        std::vector<operations_research::MPVariable *> feasible_solutions;
        for (auto i : map_seq_to_vars)
        {
            boost::dynamic_bitset<> seq_bitset = i.first;
            operations_research::MPVariable *var = i.second;

            if (var->solution_value() > 0.0)
            {
                std::cout << decode_bitset(seq_bitset) << " with solution value " << var->solution_value() << std::endl;
                this->log_buffer << decode_bitset(seq_bitset) << " with solution value " << var->solution_value() << std::endl;

                feasible_solutions.push_back(var);
            }
        }

        map_seq_to_vars.clear();

        std::size_t len_solutions = feasible_solutions.size();
        std::cout << "Number of feasible candidate guides: " << len_solutions << std::endl;
        this->log_buffer << "Number of feasible candidate guides: " << len_solutions << std::endl;
        // --------------------------------------------------
        // -------------- RANDOMIZED ROUND ------------------
        // --------------------------------------------------
        std::vector<std::pair<std::string, std::string>> solution_set;

        if (len_solutions > 0)
        {
            solution_set = randomized_rounding(
                feasible_solutions,
                this->all_containers_bitset,
                this->coversets,
                this->num_containers,
                this->num_trials,
                this->log_buffer);
        }
        else
        {
            // Empty -- A problem with 0 feasible solutions (empty inputs or no guides in the fasta files)
            //  still returns an OPTIMAL status by GLOP. Return an empty vector in this edge case.
            // Why would you input no guides? :/
            solution_set = std::vector<std::pair<std::string, std::string>>();
        }

        log_info(this->log_buffer, this->output_directory);
        return solution_set;
    }
}