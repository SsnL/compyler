/* -*- mode: C++; c-file-style: "stroustrup"; indent-tabs-mode: nil; -*- */

/* stmts.cc: AST subclasses related to statements and modules. */

/* Authors:  YOUR NAMES HERE */

#include <iostream>
#include "apyc.h"
#include "ast.h"
#include "apyc-parser.hh"

using namespace std;

static GCINIT _gcdummy;

class Region_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Region_AST, AST_Tree);

    /** By default, a Suite AST_Tree does not do anything
     *  collecting declarations. Will be overloaded by trees
     *  that will actually add appropriate declarations to
     *  ENCLOSING. */
    void collectDecls(Decl* enclosing) {
        // do nothing
    }

    /** Simple overloading that makes it easy to associate
     *  Suite AST_Tree's with a corresponding declaration. */
    Decl* getDecl(int k = 0) {
        return _me;
    }

    void addDecl(Decl* decl) {
        _me = decl;
    }

private:

    Decl *_me;
};

/*****   ASSIGN   *****/
class Assign_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Assign_AST, AST_Tree);

    void collectDecls (Decl* enclosing) {
        // Specify that left hand side is a target
        child(0)->addTargetDecls(enclosing);
        // Recursively collect declarations from right hand side
        child(1)->collectDecls(enclosing);
    }

    AST_Ptr resolveTypes (Decl* context, int& resolved, int& ambiguities,
                          bool& errors) {
        // child(0)->resolveTypes(context, resolved, ambiguities, errors);
        // child(1)->resolveTypes(context, resolved, ambiguities, errors);
        // Unwind_Stack unwind_stack;
        // if (child(0)->getType() != NULL) {
        //     // Single target: unify
        //     Type_Ptr left = child(0)->getType();
        //     Type_Ptr right = child(1)->getType();
        //     left->unify(right, unwind_stack);
        // } else {
        //     // Multiple targets: unify lists
        //     child(0)->arity();
        //     for_each_child (c, child(0)) {
        //         args[c_i_] = c->getType();
        //     } end_for;
        //     AST_Ptr typelist = make_tree(TYPE_LIST, args, args + arity);
        //     Type::unifyLists(typelist, child(1)->getType(), unwind_stack);
        // }

        return this;
    }
};

NODE_FACTORY (Assign_AST, ASSIGN);

/***** STMT_LIST *****/

/** A list of statements. */
class StmtList_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (StmtList_AST, AST_Tree);

    AST_Ptr doInnerSemantics () {
        for_each_child_var (c, this) {
            c = c->doInnerSemantics ();
        } end_for;
        return this;
    }
};

NODE_FACTORY (StmtList_AST, STMT_LIST);

/*****   PRINTLN   *****/

/** A print statement without trailing comma. */
class Println_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Println_AST, AST_Tree);

    const char* externalName () {
        return "println";
    }

};

NODE_FACTORY (Println_AST, PRINTLN);

/*****   FOR   *****/
class For_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (For_AST, AST_Tree);

    void collectDecls (Decl* enclosing) {
        // Add declarations for variable in head
        child(0)->addTargetDecls(enclosing);
        // Recursively collect declarations for body
        child(1)->collectDecls(enclosing);
        child(2)->collectDecls(enclosing);
        child(3)->collectDecls(enclosing);
    }
};

NODE_FACTORY (For_AST, FOR);

/*****   DEF   *****/
class Def_AST : public Region_AST {
protected:

    NODE_CONSTRUCTORS (Def_AST, Region_AST);

    virtual Environ* setupEnviron () {
        return new Environ(curr_environ);
    }

    AST_Ptr doInnerSemantics () {
        Decl *decl = getDecl();
        Environ* prev_environ = curr_environ;
        curr_environ = setupEnviron();
        // Collect and resolve formals
        child(1)->collectDecls(decl);
        child(1)->resolveSimpleIds(curr_environ);
        // Collect and resolve return type
        child(2)->collectDecls(decl);
        child(2)->resolveSimpleIds(curr_environ);
        // Collect and resolve function body
        for_each_child (c, child(3)) {
            c->collectDecls(decl);
            c->resolveSimpleIds(curr_environ);
        } end_for;
        // Recursively do inner semantic analysis on function body
        child(3)->doInnerSemantics();
        // Change type from a ordinary type to a function_type
        _fixType();
        curr_environ = prev_environ;
        return this;
    }

    void _fixType() {
        Decl *decl = getDecl();
        // Set the return type
        AST_Ptr returntype = Type::makeVar();
        // Set the type_list of param types
        AST_Ptr typelist = consTree(TYPE_LIST);
        int arity = child(1)->arity();
        if (arity > 0) {
            AST_Ptr *args = new AST_Ptr[arity];
            for (int i = 0; i < arity; i++) {
                args[i] = Type::makeVar();
            }
            typelist = make_tree(TYPE_LIST, args, args + arity);
        }
        // Make a new function type AST
        AST_Ptr functype = consTree(FUNCTION_TYPE, returntype, typelist);
        // Set decl's type to the type AST
        decl->setType(functype->asType());
    }

    void collectDecls (Decl* enclosing) {
        Decl* decl = enclosing->addDefDecl(child(0));
        curr_environ->define(decl);
        addDecl(decl);
    }

    void resolveSimpleIds (const Environ* env) {
        child(0)->resolveSimpleIds(env);
    }

    AST_Ptr rewriteSimpleTypes (const Environ* unused) {
        const Environ *env = getDecl()->getEnviron();
        for_each_child_var (c, this) {
            if (c_i_ != 0) {
                c = c->rewriteSimpleTypes(env);
            }
        } end_for;
        return this;
    }
};

NODE_FACTORY (Def_AST, DEF);

/*****   METHOD   *****/

class Method_AST : public Def_AST {
protected:

    NODE_CONSTRUCTORS (Method_AST, Def_AST);

    Environ* setupEnviron () {
        return new Environ(curr_environ->get_enclosure());
    }
};

NODE_FACTORY (Method_AST, METHOD);


/*****   CLASS   *****/
class Class_AST : public Region_AST {
protected:

    NODE_CONSTRUCTORS (Class_AST, Region_AST);

    AST_Ptr doInnerSemantics () {
        Decl* decl = getDecl();
        Environ* prev_environ = curr_environ;
        curr_environ = new Environ(curr_environ);
        // Collect and resolve class type parameters
        child(1)->collectDecls(decl);
        child(1)->resolveSimpleIds(curr_environ);
        // Collect and resolve class body
        for_each_child (c, child(2)) {
            c->collectDecls(decl);
            c->resolveSimpleIds(curr_environ);
        } end_for;
        // Recursively do inner semantic analysis on class body
        child(2)->doInnerSemantics();
        // Add an __init__ method if required
        _rewriteInit();
        curr_environ = prev_environ;
        return this;
    }

    void _rewriteInit () {
        Decl* decl = getDecl();
        Decl* init = decl->getEnviron()->find_immediate("__init__");
        if (init == NULL) {
            // Create AST for init
            AST_Ptr id = make_token(ID, 8, "__init__");
            AST_Ptr formals = consTree(FORMALS_LIST, make_token(ID, 4, "self"));
            AST_Ptr type = consTree(EMPTY);
            AST_Ptr block = consTree(BLOCK, consTree(STMT_LIST));
            AST_Ptr init = consTree(METHOD, id, formals, type, block);
            // Add AST class body
            child(2)->insert(0, init);
            // Collect and resolve
            init->collectDecls(decl);
            init->resolveSimpleIds(curr_environ);
            init->doInnerSemantics();
        }

    }

    void collectDecls (Decl* enclosing) {
        gcstring name = child(0)->as_token()->as_string();
        Decl* decl = makeClassDecl(name , child(1));
        enclosing->addMember(decl);
        curr_environ->define(decl);
        addDecl(decl);
    }

    void resolveSimpleIds (const Environ* env) {
        child(0)->resolveSimpleIds(env);
    }

    AST_Ptr rewriteSimpleTypes (const Environ* unused) {
        const Environ *env = getDecl()->getEnviron();
        for_each_child_var (c, this) {
            if (c_i_ != 0) {
                c = c->rewriteSimpleTypes(env);
            }
        } end_for;
        return this;
    }
};

NODE_FACTORY (Class_AST, CLASS);

/*****   FORMALS_LIST   *****/
class Formal_List_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Formal_List_AST, AST_Tree);

    void collectDecls (Decl* enclosing) {
        // Specify that children are parameters
        for_each_child (c, this) {
            c->addParamDecls(enclosing, c_i_);
        } end_for;
    }
};

NODE_FACTORY (Formal_List_AST, FORMALS_LIST);

/*****   BLOCK   *****/

/** A list of statements. */
class Block_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Block_AST, AST_Tree);

    AST_Ptr doInnerSemantics () {
        for_each_child_var (c, this) {
            c = c->doInnerSemantics ();
        } end_for;
        return this;
    }
};

NODE_FACTORY (Block_AST, BLOCK);

/*****   CLASS_BLOCK   *****/

/** A list of statements. */
class Class_Block_AST : public Block_AST {
protected:

    NODE_CONSTRUCTORS (Class_Block_AST, Block_AST);
};

NODE_FACTORY (Class_Block_AST, CLASS_BLOCK);

/*****   TARGET_LIST   *****/
class Target_List_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Target_List_AST, AST_Tree);

    Type_Ptr getType() {
        return NULL;
    }
};

NODE_FACTORY (Target_List_AST, TARGET_LIST);
