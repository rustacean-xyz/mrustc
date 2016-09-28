/*
 */
#include "expr.hpp"
#include "ast.hpp"

namespace AST {

    
Expr::Expr(unique_ptr<ExprNode> node):
    m_node(node.release())
{
}
Expr::Expr(ExprNode* node):
    m_node(node)
{
}
Expr::Expr():
    m_node(nullptr)
{
}
void Expr::visit_nodes(NodeVisitor& v)
{
    if( m_node )
    {
        m_node->visit(v);
    }
}
void Expr::visit_nodes(NodeVisitor& v) const
{
    if( m_node )
    {
        assert(v.is_const());
        //const_cast<const ExprNode*>(m_node.get())->visit(v);
        m_node->visit(v);
    }
}

Expr Expr::clone() const
{
    if( m_node ) {
        return Expr( m_node->clone() );
    }
    else {
        return Expr();
    }
}

::std::ostream& operator<<(::std::ostream& os, const Expr& pat)
{
    if( pat.m_node.get() )
        return os << *pat.m_node;
    else
        return os << "/* null */";
}

::std::ostream& operator<<(::std::ostream& os, const ExprNode& node)
{
    if( static_cast<const void*>(&node) != nullptr ) {
        node.print(os);
    }
    else {
        os << "/* NULLPTR */";
    }
    return os;
}
ExprNode::~ExprNode() {
}

#define NODE(class, _print, _clone)\
    void class::visit(NodeVisitor& nv) { nv.visit(*this); } \
    void class::print(::std::ostream& os) const _print \
    ::std::unique_ptr<ExprNode> class::clone() const _clone
#define OPT_CLONE(node) (node.get() ? node->clone() : ::AST::ExprNodeP())

namespace {
    static inline ExprNodeP mk_exprnodep(const Position& pos, AST::ExprNode* en) {
        en->set_pos(pos);
        return ExprNodeP(en);
    }
    #define NEWNODE(type, ...)  mk_exprnodep(get_pos(), new type(__VA_ARGS__))
}

NODE(ExprNode_Block, {
    os << "{";
    for(const auto& n : m_nodes)
        os << *n << ";";
    os << "}";
},{
    ::std::vector<ExprNodeP>    nodes;
    for(const auto& n : m_nodes)
        nodes.push_back( n->clone() );
    if( m_local_mod )
        TODO(get_pos(), "Handle cloning ExprNode_Block with a module");
    return NEWNODE(ExprNode_Block, m_is_unsafe, m_yields_final_value, mv$(nodes), nullptr);
})

NODE(ExprNode_Macro, {
    os << m_name << "!";
    if( m_ident.size() > 0 )
    {
        os << " " << m_ident << " ";
    }
    os << "(" << ")";
},{
    return NEWNODE(ExprNode_Macro, m_name, m_ident, m_tokens.clone());
})

NODE(ExprNode_Flow, {
    switch(m_type)
    {
    case RETURN:    os << "return"; break;
    case BREAK:     os << "break"; break;
    case CONTINUE:  os << "continue"; break;
    }
    os << " " << *m_value;
},{
    return NEWNODE(ExprNode_Flow, m_type, m_target, m_value->clone());
})


NODE(ExprNode_LetBinding, {
    os << "let " << m_pat << ": " << m_type << " = " << *m_value;
},{
    return NEWNODE(ExprNode_LetBinding, m_pat.clone(), TypeRef(m_type), OPT_CLONE(m_value));
})

NODE(ExprNode_Assign, {
    os << *m_slot << " = " << *m_value;
},{
    return NEWNODE(ExprNode_Assign, m_op, m_slot->clone(), m_value->clone());
})

NODE(ExprNode_CallPath, {
    os << m_path << "(";
    for(const auto& a : m_args) {
        os << *a << ",";
    }
    os << ")";
},{
    ::std::vector<ExprNodeP>    args;
    for(const auto& a : m_args) {
        args.push_back( a->clone() );
    }
    return NEWNODE(ExprNode_CallPath, AST::Path(m_path), mv$(args));
})

NODE(ExprNode_CallMethod, {
    os << "(" << *m_val << ")." << m_method << "(";
    for(const auto& a : m_args) {
        os << *a << ",";
    }
    os << ")";
},{
    ::std::vector<ExprNodeP>    args;
    for(const auto& a : m_args) {
        args.push_back( a->clone() );
    }
    return NEWNODE(ExprNode_CallMethod, m_val->clone(), m_method, mv$(args));
})

NODE(ExprNode_CallObject, {
    os << "(" << *m_val << ")(";
    for(const auto& a : m_args) {
        os << *a << ",";
    }
    os << ")";
},{
    ::std::vector<ExprNodeP>    args;
    for(const auto& a : m_args) {
        args.push_back( a->clone() );
    }
    return NEWNODE(ExprNode_CallObject, m_val->clone(), mv$(args));
})

NODE(ExprNode_Loop, {
    os << "LOOP [" << m_label << "] " << m_pattern << " in/= " << *m_cond << " " << *m_code;
},{
    return NEWNODE(ExprNode_Loop, m_label, m_type, m_pattern.clone(), OPT_CLONE(m_cond), m_code->clone());
})

NODE(ExprNode_Match, {
    os << "match ("<<*m_val<<") {";
    for(const auto& arm : m_arms)
    {
        for( const auto& pat : arm.m_patterns )
            os << " " << pat;
        os << " if " << *arm.m_cond;
        
        os << " => " << *arm.m_code << ",";
    }
    os << "}";
},{
    ::std::vector< ExprNode_Match_Arm>  arms;
    for(const auto& arm : m_arms) {
        ::std::vector< AST::Pattern>    patterns;
        for( const auto& pat : arm.m_patterns ) {
            patterns.push_back( pat.clone() );
        }
        arms.push_back( ExprNode_Match_Arm( mv$(patterns), OPT_CLONE(arm.m_cond), arm.m_code->clone() ) );
        arms.back().m_attrs = arm.m_attrs.clone();
    }
    return NEWNODE(ExprNode_Match, m_val->clone(), mv$(arms));
})

NODE(ExprNode_If, {
    os << "if " << *m_cond << " { " << *m_true << " } else { " << *m_false << " }";
},{
    return NEWNODE(ExprNode_If, m_cond->clone(), m_true->clone(), OPT_CLONE(m_false));
})
NODE(ExprNode_IfLet, {
    os << "if let " << m_pattern << " = (" << *m_value << ") { " << *m_true << " } else { " << *m_false << " }";
},{
    return NEWNODE(ExprNode_IfLet, m_pattern.clone(), m_value->clone(), m_true->clone(), OPT_CLONE(m_false));
})

NODE(ExprNode_Integer, {
    os << m_value;
},{
    return NEWNODE(ExprNode_Integer, m_value, m_datatype);
})
NODE(ExprNode_Float, {
    os << m_value;
},{
    return NEWNODE(ExprNode_Float, m_value, m_datatype);
})
NODE(ExprNode_Bool, {
    os << m_value;
},{
    return NEWNODE(ExprNode_Bool, m_value);
})
NODE(ExprNode_String, {
    os << "\"" << m_value << "\"";
},{
    return NEWNODE(ExprNode_String, m_value);
})
NODE(ExprNode_ByteString, {
    os << "b\"" << m_value << "\"";
},{
    return NEWNODE(ExprNode_ByteString, m_value);
})

NODE(ExprNode_Closure, {
    os << "/* todo: closure */";
},{
    ExprNode_Closure::args_t    args;
    for(const auto& a : m_args) {
        args.push_back( ::std::make_pair(a.first.clone(), TypeRef(a.second)) );
    }
    return NEWNODE(ExprNode_Closure, mv$(args), TypeRef(m_return), m_code->clone());
});

NODE(ExprNode_StructLiteral, {
    os << "/* todo: sl */";
},{
    ExprNode_StructLiteral::t_values    vals;
    
    for(const auto& v : m_values) {
        vals.push_back( ::std::make_pair(v.first, v.second->clone()) );
    }
    
    return NEWNODE(ExprNode_StructLiteral, AST::Path(m_path), OPT_CLONE(m_base_value), mv$(vals) );
})

NODE(ExprNode_Array, {
    os << "[";
    if( m_size.get() )
        os << *m_values[0] << "; " << *m_size;
    else
        for(const auto& a : m_values)
            os << *a << ",";
    os << "]";
},{
    if( m_size.get() )
    {
        return NEWNODE(ExprNode_Array, m_values[0]->clone(), m_size->clone());
    }
    else
    {
        ::std::vector<ExprNodeP>    nodes;
        for(const auto& n : m_values)
            nodes.push_back( n->clone() );
        return NEWNODE(ExprNode_Array, mv$(nodes));
    }
})

NODE(ExprNode_Tuple, {
    os << "(";
    for(const auto& a : m_values) {
        os << *a << ",";
    }
    os << ")";
},{
    ::std::vector<ExprNodeP>    nodes;
    for(const auto& n : m_values)
        nodes.push_back( n->clone() );
    return NEWNODE(ExprNode_Tuple, mv$(nodes));
})

NODE(ExprNode_NamedValue, {
    os << m_path;
},{
    return NEWNODE(ExprNode_NamedValue, AST::Path(m_path));
})

NODE(ExprNode_Field, {
    os << "(" << *m_obj << ")." << m_name;
},{
    return NEWNODE(ExprNode_Field, m_obj->clone(), m_name);
})

NODE(ExprNode_Index, {
    os << "(" << *m_obj << ")[" << *m_idx << "]";
},{
    return NEWNODE(ExprNode_Index, m_obj->clone(), m_idx->clone());
})

NODE(ExprNode_Deref, {
    os << "*(" << *m_value << ")";
},{
    return NEWNODE(ExprNode_Deref, m_value->clone());
});

NODE(ExprNode_Cast, {
    os << "(" << *m_value << " as " << m_type << ")";
},{
    return NEWNODE(ExprNode_Cast, m_value->clone(), TypeRef(m_type));
})

NODE(ExprNode_BinOp, {
    os << "(" << *m_left << " ";
    switch(m_type)
    {
    case CMPEQU:    os << "=="; break;
    case CMPNEQU:   os << "!="; break;
    case CMPLT:     os << "<";  break;
    case CMPLTE:    os << "<="; break;
    case CMPGT:     os << ">";  break;
    case CMPGTE:    os << ">="; break;
    case BOOLAND:   os << "&&"; break;
    case BOOLOR:    os << "||"; break;
    case BITAND:    os << "&"; break;
    case BITOR:     os << "|"; break;
    case BITXOR:    os << "^"; break;
    case SHR:    os << ">>"; break;
    case SHL:    os << "<<"; break;
    case MULTIPLY: os << "*"; break;
    case DIVIDE:   os << "/"; break;
    case MODULO:   os << "%"; break;
    case ADD:   os << "+"; break;
    case SUB:   os << "-"; break;
    case RANGE:   os << ".."; break;
    case RANGE_INC:   os << "..."; break;
    case PLACE_IN:  os << "<-"; break;
    }
    os << " " << *m_right << ")";
},{
    return NEWNODE(ExprNode_BinOp, m_type, OPT_CLONE(m_left), OPT_CLONE(m_right));
})

NODE(ExprNode_UniOp, {
    switch(m_type)
    {
    case NEGATE: os << "(-"; break;
    case INVERT: os << "(!"; break;
    case BOX: os << "(box "; break;
    case REF: os << "(&"; break;
    case REFMUT: os << "(&mut "; break;
    case QMARK: os << "(" << *m_value << "?)"; return;
    }
    os << *m_value << ")";
},{
    return NEWNODE(ExprNode_UniOp, m_type, m_value->clone());
})


#define NV(type, actions)\
    void NodeVisitorDef::visit(type& node) { /*DEBUG("DEF - "#type);*/ actions }
//  void NodeVisitorDef::visit(const type& node) { DEBUG("DEF - "#type" (const)"); actions }

NV(ExprNode_Block, {
    //INDENT();
    for( auto& child : node.m_nodes )
        visit(child);
    //UNINDENT();
})
NV(ExprNode_Macro,
{
    BUG(node.get_pos(), "Hit unexpanded macro in expression - " << node);
})
NV(ExprNode_Flow,
{
    visit(node.m_value);
})
NV(ExprNode_LetBinding,
{
    // TODO: Handle recurse into Let pattern?
    visit(node.m_value);
})
NV(ExprNode_Assign,
{
    INDENT();
    visit(node.m_slot);
    visit(node.m_value);
    UNINDENT();
})
NV(ExprNode_CallPath,
{
    INDENT();
    for( auto& arg : node.m_args )
        visit(arg);
    UNINDENT();
})
NV(ExprNode_CallMethod,
{
    INDENT();
    visit(node.m_val);
    for( auto& arg : node.m_args )
        visit(arg);
    UNINDENT();
})
NV(ExprNode_CallObject,
{
    INDENT();
    visit(node.m_val);
    for( auto& arg : node.m_args )
        visit(arg);
    UNINDENT();
})
NV(ExprNode_Loop,
{
    INDENT();
    visit(node.m_cond);
    visit(node.m_code);
    UNINDENT();
})
NV(ExprNode_Match,
{
    INDENT();
    visit(node.m_val);
    for( auto& arm : node.m_arms )
    {
        visit(arm.m_cond);
        visit(arm.m_code);
    }
    UNINDENT();
})
NV(ExprNode_If,
{
    INDENT();
    visit(node.m_cond);
    visit(node.m_true);
    visit(node.m_false);
    UNINDENT();
})
NV(ExprNode_IfLet,
{
    INDENT();
    visit(node.m_value);
    visit(node.m_true);
    visit(node.m_false);
    UNINDENT();
})

NV(ExprNode_Integer, {(void)node;})
NV(ExprNode_Float, {(void)node;})
NV(ExprNode_Bool, {(void)node;})
NV(ExprNode_String, {(void)node;})
NV(ExprNode_ByteString, {(void)node;})

NV(ExprNode_Closure,
{
    visit(node.m_code);
});
NV(ExprNode_StructLiteral,
{
    visit(node.m_base_value);
    for( auto& val : node.m_values )
        visit(val.second);
})
NV(ExprNode_Array,
{
    visit(node.m_size);
    for( auto& val : node.m_values )
        visit(val);
})
NV(ExprNode_Tuple,
{
    for( auto& val : node.m_values )
        visit(val);
})
NV(ExprNode_NamedValue,
{
    (void)node;
    // LEAF
})

NV(ExprNode_Field,
{
    visit(node.m_obj);
})
NV(ExprNode_Index,
{
    visit(node.m_obj);
    visit(node.m_idx);
})
NV(ExprNode_Deref,
{
    visit(node.m_value);
})
NV(ExprNode_Cast,
{
    visit(node.m_value);
})
NV(ExprNode_BinOp,
{
    visit(node.m_left);
    visit(node.m_right);
})
NV(ExprNode_UniOp,
{
    visit(node.m_value);
})
#undef NV


};

