/*++
Copyright (c) 2014 Microsoft Corporation

Module Name:

    card2bv_tactic.cpp

Abstract:

    Tactic for converting Pseudo-Boolean constraints to BV

Author:

    Nikolaj Bjorner (nbjorner) 2014-03-20

Notes:

--*/
#include"tactical.h"
#include"cooperate.h"
#include"rewriter_def.h"
#include"ast_smt2_pp.h"
#include"expr_substitution.h"
#include"card2bv_tactic.h"
#include"pb_rewriter.h"

namespace pb {
    unsigned card2bv_rewriter::get_num_bits(func_decl* f) {
        rational r(0);
        unsigned sz = f->get_arity();
        for (unsigned i = 0; i < sz; ++i) {
            r += pb.get_coeff(f, i);
        }
        r = r > pb.get_k(f)? r : pb.get_k(f);
        return r.get_num_bits();
    }
    
    card2bv_rewriter::card2bv_rewriter(ast_manager& m):
        m(m),
        au(m),
        pb(m),
        bv(m)
    {}
    
    br_status card2bv_rewriter::mk_app_core(func_decl * f, unsigned sz, expr * const* args, expr_ref & result) {
        if (f->get_family_id() == null_family_id) {
            if (sz == 1) {
                // Expecting minimize/maximize.
                func_decl_ref fd(m);
                fd = m.mk_func_decl(f->get_name(), m.get_sort(args[0]), f->get_range());
                result = m.mk_app(fd.get(), args[0]);
                return BR_DONE;
            }
            else
                return BR_FAILED;
        }
        else if (f->get_family_id() == m.get_basic_family_id()) {
            result = m.mk_app(f, sz, args);
            return BR_DONE;
        }
        else if (f->get_family_id() == pb.get_family_id()) {
            br_status st = mk_shannon(f, sz, args, result);
            if (st == BR_FAILED) {
                return mk_bv(f, sz, args, result);
            }
            else {
                return st;
            }
        }
        // NSB: review
        // we should remove this code and rely on a layer above to deal with 
        // whatever it accomplishes. It seems to break types.
        // 
        else if (f->get_family_id() == au.get_family_id()) {
            if (f->get_decl_kind() == OP_ADD) {
                unsigned bits = 0;
                for (unsigned i = 0; i < sz; i++) {
                    rational val1, val2;
                    if (au.is_int(args[i]) && au.is_numeral(args[i], val1)) {
                        bits += val1.get_num_bits();
                    }
                    else if (m.is_ite(args[i]) &&
                             au.is_numeral(to_app(args[i])->get_arg(1), val1) && val1.is_one() &&
                             au.is_numeral(to_app(args[i])->get_arg(2), val2) && val2.is_zero()) {
                        bits++;                        
                    }
                    else
                        return BR_FAILED;
                }
                
                result = 0;
                for (unsigned i = 0; i < sz; i++) {
                    rational val1, val2;
                    expr * q;
                    if (au.is_int(args[i]) && au.is_numeral(args[i], val1))
                        q = bv.mk_numeral(val1, bits);
                    else
                        q = m.mk_ite(to_app(args[i])->get_arg(0), bv.mk_numeral(1, bits), bv.mk_numeral(0, bits));
                    result = (i == 0) ? q : bv.mk_bv_add(result.get(), q);
                }                
                return BR_DONE;
            }
            else 
                return BR_FAILED;
        }
        else
            return BR_FAILED;
    }


    br_status card2bv_rewriter::mk_bv(func_decl * f, unsigned sz, expr * const* args, expr_ref & result) {
        expr_ref zero(m), a(m), b(m);
        expr_ref_vector es(m);
        unsigned bw = get_num_bits(f);
        zero = bv.mk_numeral(rational(0), bw);
        for (unsigned i = 0; i < sz; ++i) {
            es.push_back(m.mk_ite(args[i], bv.mk_numeral(pb.get_coeff(f, i), bw), zero));
        }
        switch (es.size()) {
        case 0:  a = zero; break;
        case 1:  a = es[0].get(); break;
        default:
            a = es[0].get();
            for (unsigned i = 1; i < es.size(); ++i) {
                a = bv.mk_bv_add(a, es[i].get());
            }
            break;
        }
        b = bv.mk_numeral(pb.get_k(f), bw);
        
        switch (f->get_decl_kind()) {
        case OP_AT_MOST_K:
        case OP_PB_LE:
            UNREACHABLE();
            result = bv.mk_ule(a, b);
            break;
        case OP_AT_LEAST_K:
            UNREACHABLE();
        case OP_PB_GE:
            result = bv.mk_ule(b, a);
            break;
        case OP_PB_EQ:
            result = m.mk_eq(a, b);
            break;
        default:
            UNREACHABLE();
        }
        return BR_DONE;
    }

    struct argc_t {
        expr*    m_arg;
        rational m_coeff;
        argc_t():m_arg(0), m_coeff(0) {}
        argc_t(expr* arg, rational const& r): m_arg(arg), m_coeff(r) {}
    };
    struct argc_gt {
        bool operator()(argc_t const& a, argc_t const& b) const {
            return a.m_coeff > b.m_coeff;
        }
    };
    struct argc_entry {
        unsigned m_index;
        rational m_k;
        expr*    m_value;
        argc_entry(unsigned i, rational const& k): m_index(i), m_k(k), m_value(0) {}
        argc_entry():m_index(0), m_k(0), m_value(0) {}

        struct eq {
            bool operator()(argc_entry const& a, argc_entry const& b) const {
                return a.m_index == b.m_index && a.m_k == b.m_k;
            }
        };
        struct hash {
            unsigned operator()(argc_entry const& a) const {
                return a.m_index ^ a.m_k.hash();
            }
        };
    };
    typedef hashtable<argc_entry, argc_entry::hash, argc_entry::eq> argc_cache;
    
    br_status card2bv_rewriter::mk_shannon(
        func_decl * f, unsigned sz, expr * const* args, expr_ref & result) {

        return BR_FAILED;

        // disabled for now.
        unsigned max_clauses = sz*10;
        vector<argc_t> argcs;
        for (unsigned i = 0; i < sz; ++i) {
            argcs.push_back(argc_t(args[i], pb.get_coeff(f, i)));
        }
        std::sort(argcs.begin(), argcs.end(), argc_gt());
        DEBUG_CODE(
            for (unsigned i = 0; i + 1 < sz; ++i) {
                SASSERT(argcs[i].m_coeff >= argcs[i+1].m_coeff);
            }
            );
        argc_cache cache;
        expr_ref_vector trail(m);
        vector<rational> todo_k;
        unsigned_vector  todo_i;
        todo_k.push_back(pb.get_k(f));
        todo_i.push_back(0);
        decl_kind kind = f->get_decl_kind();
        argc_entry entry1;
        while (!todo_i.empty()) {

            if (cache.size() > max_clauses) {
                return BR_FAILED;
            }
            unsigned i = todo_i.back();
            rational k = todo_k.back();
            argc_entry entry(i, k);
            if (cache.contains(entry)) {
                todo_i.pop_back();
                todo_k.pop_back();
                continue;
            }
            SASSERT(i < sz);
            SASSERT(!k.is_neg());
            rational const& coeff = argcs[i].m_coeff;
            expr* arg = argcs[i].m_arg;
            if (i + 1 == sz) {
                switch(kind) {
                case OP_AT_MOST_K:
                case OP_PB_LE:
                    if (coeff <= k) {
                        entry.m_value = m.mk_true();
                    }
                    else {
                        entry.m_value = negate(arg);
                        trail.push_back(entry.m_value);
                    }
                    break;
                case OP_AT_LEAST_K:
                case OP_PB_GE:
                    if (coeff < k) {
                        entry.m_value = m.mk_false();
                    }
                    else if (coeff.is_zero()) {
                        entry.m_value = m.mk_true();
                    }
                    else {
                        entry.m_value = arg;
                    }
                    break;
                case OP_PB_EQ:
                    if (coeff == k) {
                        entry.m_value = arg;
                    }
                    else if (k.is_zero()) {
                        entry.m_value = negate(arg);
                        trail.push_back(entry.m_value);
                    }
                    else {
                        entry.m_value = m.mk_false();
                    }
                    break;
                }
                todo_i.pop_back();
                todo_k.pop_back();
                cache.insert(entry);
                continue;
            }
            entry.m_index++;        
            expr* lo = 0, *hi = 0;
            if (cache.find(entry, entry1)) {
                lo = entry1.m_value;                
            }
            else {
                todo_i.push_back(i+1);
                todo_k.push_back(k);
            }
            entry.m_k -= coeff;
            if (kind != OP_PB_EQ && entry.m_k.is_neg()) {
                switch (kind) {
                case OP_AT_MOST_K:
                case OP_PB_LE:
                    hi = m.mk_false();
                    break;
                case OP_AT_LEAST_K:
                case OP_PB_GE:
                    hi = m.mk_true();
                    break;
                default:
                    UNREACHABLE();
                }
            }
            else if (cache.find(entry, entry1)) {
                hi = entry1.m_value;
            }
            else {
                todo_i.push_back(i+1);
                todo_k.push_back(entry.m_k);
            }
            if (hi && lo) {
                todo_i.pop_back();
                todo_k.pop_back();
                entry.m_index = i;
                entry.m_k = k;
                entry.m_value = mk_ite(arg, hi, lo);
                trail.push_back(entry.m_value);
                cache.insert(entry);
            }
        }        
        argc_entry entry(0, pb.get_k(f));
        VERIFY(cache.find(entry, entry));
        result = entry.m_value;
        return BR_DONE;
    }

    expr* card2bv_rewriter::negate(expr* e) {
        if (m.is_not(e, e)) return e;
        return m.mk_not(e);
    }

    expr* card2bv_rewriter::mk_ite(expr* c, expr* hi, expr* lo) {
        if (hi == lo) return hi;
        if (m.is_true(hi) && m.is_false(lo)) return c;
        if (m.is_false(hi) && m.is_true(lo)) return negate(c);
        if (m.is_true(hi)) return m.mk_or(c, lo);
        if (m.is_false(lo)) return m.mk_and(c, hi);
        if (m.is_false(hi)) return m.mk_and(negate(c), lo);
        if (m.is_true(lo)) return m.mk_implies(c, hi);
        return m.mk_ite(c, hi, lo);
    }
};

template class rewriter_tpl<pb::card2bv_rewriter_cfg>;

class card2bv_tactic : public tactic {
    ast_manager &              m;
    params_ref                 m_params;
    th_rewriter                m_rw1;
    pb::card_pb_rewriter       m_rw2;
    
public:

    card2bv_tactic(ast_manager & m, params_ref const & p):
        m(m),
        m_params(p),
        m_rw1(m),
        m_rw2(m) {
    }

    virtual tactic * translate(ast_manager & m) {
        return alloc(card2bv_tactic, m, m_params);
    }

    virtual ~card2bv_tactic() {
    }

    virtual void updt_params(params_ref const & p) {
        m_params = p;
    }

    virtual void collect_param_descrs(param_descrs & r) {  
    }

    void set_cancel(bool f) {
        m_rw1.set_cancel(f);
        m_rw2.set_cancel(f);
    }
    
    virtual void operator()(goal_ref const & g, 
                            goal_ref_buffer & result, 
                            model_converter_ref & mc, 
                            proof_converter_ref & pc,
                            expr_dependency_ref & core) {
        TRACE("card2bv-before", g->display(tout););
        SASSERT(g->is_well_sorted());
        fail_if_proof_generation("card2bv", g);
        mc = 0; pc = 0; core = 0; result.reset();
        tactic_report report("card2bv", *g);
        m_rw1.reset(); 
        m_rw2.reset(); 
        
        if (g->inconsistent()) {
            result.push_back(g.get());
            return;
        }
                
        unsigned size = g->size();
        expr_ref new_f1(m), new_f2(m);
        for (unsigned idx = 0; idx < size; idx++) {
            m_rw1(g->form(idx), new_f1);
            TRACE("card2bv", tout << "Rewriting " << mk_ismt2_pp(new_f1.get(), m) << std::endl;);
            m_rw2(new_f1, new_f2);
            g->update(idx, new_f2, g->pr(idx), g->dep(idx));
        }

        g->inc_depth();
        result.push_back(g.get());
        TRACE("card2bv", g->display(tout););
        SASSERT(g->is_well_sorted());
    }
    
    virtual void cleanup() {
    }
};

tactic * mk_card2bv_tactic(ast_manager & m, params_ref const & p) {
    return clean(alloc(card2bv_tactic, m, p));
}

