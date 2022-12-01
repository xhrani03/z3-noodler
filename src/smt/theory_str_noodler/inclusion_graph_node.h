/**
 * @brief Create basic representation of an inclusion graph node.
 *
 * The inclusion graph node is represented as a predicate, represention an equation, inequation or another predicate
 *  such as contains, etc.
 * Each equation or inequation consists of a left and right side of the equation which hold a vector of basic equation
 *  terms.
 * Each term is of one of the following types:
 *     - Literal,
 *     - Variable, or
 *     - operation such as IndexOf, Length, etc.
 */

#ifndef Z3_INCLUSION_GRAPH_NODE_H
#define Z3_INCLUSION_GRAPH_NODE_H

#include <utility>
#include <vector>
#include <stdexcept>
#include <memory>
#include <cassert>
#include <unordered_map>
#include <string>
#include <string_view>
#include <set>
#include <unordered_set>

namespace smt::noodler {
    enum struct PredicateType {
        Default,
        Equation,
        Inequation,
        Contains,
        // TODO: Add additional predicate types.
    };

    [[nodiscard]] static std::string to_string(PredicateType predicate_type) {
        switch (predicate_type) {
            case PredicateType::Default:
                return "Default";
            case PredicateType::Equation:
                return "Equation";
            case PredicateType::Inequation:
                return "Inequation";
            case PredicateType::Contains:
                return "Contains";
        }

        throw std::runtime_error("Unhandled predicate type passed to to_string().");
    }

    enum struct BasicTermType {
        Variable,
        Literal,
        Length,
        Substring,
        IndexOf,
        // TODO: Add additional basic term types.
    };

    [[nodiscard]] static std::string to_string(BasicTermType term_type) {
        switch (term_type) {
            case BasicTermType::Variable:
                return "Variable";
            case BasicTermType::Literal:
                return "Literal";
            case BasicTermType::Length:
                return "Length";
            case BasicTermType::Substring:
                return "Substring";
            case BasicTermType::IndexOf:
                return "IndexOf";
        }

        throw std::runtime_error("Unhandled basic term type passed to to_string().");
    }

    class BasicTerm {
    public:
        explicit BasicTerm(BasicTermType type): type(type) {}
        BasicTerm(BasicTermType type, std::string_view name): type(type), name(name) {}

        [[nodiscard]] BasicTermType get_type() const { return type; }
        [[nodiscard]] bool is_variable() const { return type == BasicTermType::Variable; }
        [[nodiscard]] bool is_literal() const { return type == BasicTermType::Literal; }
        [[nodiscard]] bool is(BasicTermType term_type) const { return type == term_type; }

        [[nodiscard]] std::string get_name() const { return name; }
        void set_name(std::string_view new_name) { name = new_name; }

        [[nodiscard]] bool equals(const BasicTerm& other) const {
            return type == other.get_type() && name == other.get_name();
        }

        [[nodiscard]] std::string to_string() const;

        struct HashFunction {
            size_t operator() (const BasicTerm& basic_term) const {
                size_t row_hash = std::hash<BasicTermType>()(basic_term.type);
                size_t col_hash = std::hash<std::string>()(basic_term.name) << 1;
                return row_hash ^ col_hash;
            }
        };

    private:
        BasicTermType type;
        std::string name;
    }; // Class BasicTerm.

    [[nodiscard]] static std::string to_string(const BasicTerm& basic_term) {
        return basic_term.to_string();
    }

    static bool operator==(const BasicTerm& lhs, const BasicTerm& rhs) { return lhs.equals(rhs); }
    static bool operator!=(const BasicTerm& lhs, const BasicTerm& rhs) { return !(lhs == rhs); }
    static bool operator<(const BasicTerm& lhs, const BasicTerm& rhs) {
        if (lhs.get_type() < rhs.get_type()) {
            return true;
        } else if (lhs.get_type() > rhs.get_type()) {
            return false;
        }
        // Types are equal. Compare names.
        if (lhs.get_name() < rhs.get_name()) {
            return true;
        }
        return false;
    }
    static bool operator>(const BasicTerm& lhs, const BasicTerm& rhs) { return !(lhs < rhs); }

    class Predicate {
    public:
        enum struct EquationSideType {
            Left,
            Right,
        };

        Predicate() : type(PredicateType::Default) {}
        explicit Predicate(const PredicateType type): type(type) {
            if (is_equation() || is_inequation()) {
                params.resize(2);
                params.emplace_back();
                params.emplace_back();
            }
        }

        explicit Predicate(const PredicateType type, std::vector<std::vector<BasicTerm>> par): 
            type(type), 
            params(par) 
            { }

        [[nodiscard]] PredicateType get_type() const { return type; }
        [[nodiscard]] bool is_equation() const { return type == PredicateType::Equation; }
        [[nodiscard]] bool is_inequation() const { return type == PredicateType::Inequation; }
        [[nodiscard]] bool is_eq_or_ineq() const { return is_equation() || is_inequation(); }
        [[nodiscard]] bool is_predicate() const { return !is_eq_or_ineq(); }
        [[nodiscard]] bool is(const PredicateType predicate_type) const { return predicate_type == this->type; }

        [[nodiscard]] std::vector<std::vector<BasicTerm>>& get_params() { return params; }
        [[nodiscard]] const std::vector<std::vector<BasicTerm>>& get_params() const { return params; }
        std::vector<BasicTerm>& get_left_side() {
            assert(is_eq_or_ineq());
            return params[0];
        }

        [[nodiscard]] const std::vector<BasicTerm>& get_left_side() const {
            assert(is_eq_or_ineq());
            return params[0];
        }

        std::vector<BasicTerm>& get_right_side() {
            assert(is_eq_or_ineq());
            return params[1];
        }

        [[nodiscard]] const std::vector<BasicTerm>& get_right_side() const {
            assert(is_eq_or_ineq());
            return params[1];
        }

        std::vector<BasicTerm>& get_side(EquationSideType side);

        [[nodiscard]] const std::vector<BasicTerm>& get_side(EquationSideType side) const;

        [[nodiscard]] Predicate get_switched_sides_predicate() const {
            assert(is_eq_or_ineq());
            return Predicate{ type, { get_right_side(), get_left_side() } };
        }

        /**
         * Get unique variables on both sides of an (in)equation.
         * @return Variables in the (in)equation.
         */
        [[nodiscard]] std::set<BasicTerm> get_vars() const;

        /**
         * Get unique variables on a single @p side of an (in)equation.
         * @param[in] side (In)Equation side to get variables from.
         * @return Variables in the (in)equation on specified @p side.
         */
        [[nodiscard]] std::set<BasicTerm> get_side_vars(EquationSideType side) const;

        /**
         * Decide whether the @p side contains multiple occurrences of a single variable (with a same name).
         * @param side Side to check.
         * @return True if there are multiple occurrences of a single variable. False otherwise.
         */
        [[nodiscard]] bool mult_occurr_var_side(EquationSideType side) const;

        void replace(void* replace_map) {
            (void) replace_map;
            throw std::runtime_error("unimplemented");
        }

        void remove(void* terms_to_remove) {
            (void) terms_to_remove;
            throw std::runtime_error("unimplemented");
        }

        [[nodiscard]] bool equals(const Predicate& other) const;

        [[nodiscard]] std::string to_string() const;

        struct HashFunction {
            size_t operator()(const Predicate& predicate) const {
                size_t res{};
                size_t row_hash = std::hash<PredicateType>()(predicate.type);
                for (const auto& term: predicate.get_left_side()) {
                    size_t col_hash = BasicTerm::HashFunction()(term) << 1;
                    res ^= col_hash;
                }
                for (const auto& term: predicate.get_right_side()) {
                    size_t col_hash = BasicTerm::HashFunction()(term) << 1;
                    res ^= col_hash;
                }
                return row_hash ^ res;
            }
        };

        // TODO: Additional operations.

    private:
        PredicateType type;
        std::vector<std::vector<BasicTerm>> params;
    }; // Class Predicate.

    [[nodiscard]] static std::string to_string(const Predicate& predicate) {
        return predicate.to_string();
    }

    static std::ostream& operator<<(std::ostream& os, const Predicate& predicate) {
        os << predicate.to_string();
        return os;
    }

    static bool operator==(const Predicate& lhs, const Predicate& rhs) { return lhs.equals(rhs); }
    static bool operator!=(const Predicate& lhs, const Predicate& rhs) { return !(lhs == rhs); }
    static bool operator<(const Predicate& lhs, const Predicate& rhs) {
        if (lhs.get_type() < rhs.get_type()) {
            return true;
        } else if (lhs.get_type() > rhs.get_type()) {
            return false;
        }
        // Types are equal. Compare data.
        if (lhs.get_params() < rhs.get_params()) {
            return true;
        }
        return false;
    }
    static bool operator>(const Predicate& lhs, const Predicate& rhs) { return !(lhs < rhs); }

    class Formula {
    
    public:
        Formula(): predicates() {}

        std::vector<Predicate>& get_predicates() { return predicates; }

        // TODO: Use std::move for both add functions?
        void add_predicate(const Predicate& predicate) { predicates.push_back(predicate); }

    private:
        std::vector<Predicate> predicates;
    }; // Class Formula.
} // Namespace smt::noodler.

namespace std {
    template<>
    struct hash<smt::noodler::Predicate> {
        inline size_t operator()(const smt::noodler::Predicate& predicate) const {
            size_t accum = smt::noodler::Predicate::HashFunction()(predicate);
            return accum;
        }
    };

    template<>
    struct hash<smt::noodler::BasicTerm> {
        inline size_t operator()(const smt::noodler::BasicTerm& basic_term) const {
            size_t accum = smt::noodler::BasicTerm::HashFunction()(basic_term);
            return accum;
        }
    };
}

#endif //Z3_INCLUSION_GRAPH_NODE_H
