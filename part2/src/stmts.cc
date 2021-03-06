/* -*- mode: C++; c-file-style: "stroustrup"; indent-tabs-mode: nil; -*- */

/* stmts.cc: AST subclasses related to statements and modules. */

/* Authors: Kevin Wu, Wenson Hsieh, Leo Kam */

#include <iostream>
#include "apyc.h"
#include "ast.h"
#include "apyc-parser.hh"

using namespace std;

static GCINIT _gcdummy;

class Region_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Region_AST, AST_Tree);

    /** By default, a Suite AST_Tree does not do anything when
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

    Type_Ptr getType() {
        if (_type == NULL) {
            _type = computeType();
        }
        return _type;
    }

    Type_Ptr computeType () {
        return getDecl()->getType();
    }

private:

    Type_Ptr _type;
    Decl *_me;
};


/********************   STMT_LIST   ********************/

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


/********************   PRINTING   ********************/
class Printing_AST : public AST_Tree {
protected:

    NODE_BASE_CONSTRUCTORS (Printing_AST, AST_Tree);

    AST_Ptr resolveTypes (Decl* context, int& resolved,
                          int& ambiguities, bool& errors) {
        for_each_child_var (c, this) {
            c = c->resolveTypes (context, resolved, ambiguities, errors);
        } end_for;

        // If printing to something, check it is a file.
        if (!child(0)->isMissing() && !child(0)->getType()->unify(
                primitiveDecls[File]->asType(), global_bindings)) {
            errors = true;
            error(loc(), "type error: can only print to a file");
        } else {
            resolved++;
        }
        return this;
    }

};


/********************   PRINT   ********************/
/** A print statement with trailing comma. */
class Print_AST : public Printing_AST {
protected:

    NODE_CONSTRUCTORS (Print_AST, Printing_AST);
};

NODE_FACTORY (Print_AST, PRINT);


/********************   PRINTLN   ********************/
/** A print statement without trailing comma. */
class Println_AST : public Printing_AST {
protected:

    NODE_CONSTRUCTORS (Println_AST, Printing_AST);

    const char* externalName () {
        return "println";
    }
};

NODE_FACTORY (Println_AST, PRINTLN);


/********************   FOR   ********************/
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

    AST_Ptr resolveTypes (Decl* context, int& resolved,
                          int& ambiguities, bool& errors) {
        for_each_child_var (c, this) {
            if (c_i_ < 2)
                c = c->resolveTypes (context, resolved, ambiguities, errors);
        } end_for;
        if (child(0)->as_string() == "target_list") {
            errors = true;
            error(loc(), "type error: cannot unpack");
        }
        Type_Ptr targetType = child(0)->getType();
        gcstring targetName = targetType->binding()->as_string().c_str();
        Type_Ptr exprType = child(1)->getType();
        gcstring exprName = exprType->binding()->as_string().c_str();

        if (exprName == List) {
            Type_Ptr elementType = exprType->binding()->typeParam(0);
            if (!targetType->unify(elementType, global_bindings)) {
                errors = true;
                error(loc(), "type error: type of %s does not match list type",
                    child(0)->as_string().c_str());
            } else {
                resolved++;
            }
        } else if (exprName == Range) {
            if (!targetType->unify(primitiveDecls[Int]->asType(),
                        global_bindings)) {
                errors = true;
                error(loc(), "type error: type of %s must be int",
                    child(0)->as_string().c_str());
            } else {
                resolved++;
            }
        } else if (exprName == Str) {
            if (!targetType->unify(primitiveDecls[Str]->asType(),
                        global_bindings)){
                errors = true;
                error(loc(), "type error: type of %s must be str",
                    child(0)->as_string().c_str());
            } else {
                resolved++;
            }
        } else {
            errors = true;
            error(loc(), "type error: object is not iterable");
        }
        for_each_child_var (c, this) {
            if (c_i_ >= 2)
                c = c->resolveTypes (context, resolved, ambiguities, errors);
        } end_for;
        return this;
    }
};

NODE_FACTORY (For_AST, FOR);


/********************   DEF   ********************/
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
        gcstring text = child(0)->as_token()->as_string();
        Decl* decl = enclosing->getEnviron()->find_immediate(text);

        if (decl == NULL || decl->isFunction()) {
            decl = enclosing->addDefDecl(child(0));
            curr_environ->define(decl);
            addDecl(decl);
        } else if (decl->isType()) {
            error(loc(), "syntax error: %s is already a class", text.c_str());
            exit(1);
        } else {
            error(loc(), "syntax error: %s is already a variable", text.c_str());
            exit(1);
        }
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

    virtual bool _setupSelf(Decl* context) {
        return true;
    }

    AST_Ptr resolveTypes (Decl* context, int& resolved,
                          int& ambiguities, bool& errors) {
        Decl* decl = getDecl();
        decl->setFrozen(true);
        if(!_setupSelf(context))
            errors = true;

        for_each_child_var (c, this) {
            c = c->resolveTypes (decl, resolved, ambiguities, errors);
        } end_for;

        Type_Ptr funcType = decl->getType();
        Type_Ptr returnType = funcType->returnType();
        Type_Ptr rType;

        if (!child(2)->isMissing())
            rType = child(2)->asType();
        else
            rType = NULL;

        // No return statement in function body
        if (returnType->binding() == returnType) {
            if (rType != NULL) {
                returnType->unify(rType, global_bindings);
            } else {
                Type_Ptr noneType = outer_environ->find("__None__")->getType()
                    ->returnType();
                returnType->unify(noneType, global_bindings);
            }
        } else if (rType != NULL && !returnType->unify(rType, global_bindings)){
            errors = true;
            error(loc(), "type error: type of return object does not match "
                "the return type of the function");
        }

        Type_Ptr paramType;

        for_each_child_var (c, child(1)) {
            paramType = c->getType();
            if (!funcType->paramType(c_i_)->unify(paramType, global_bindings)) {
                errors = true;
                error(loc(), "type error: type mismatch");
            }
        } end_for;

        if (!errors)
            resolved++;
        decl->setFrozen(false);

        return this;
    }

    /** Override AST::checkResolved() to only recurse down the body of the
     *  function */
    void checkResolved () {
        child(3)->checkResolved();
    }
};

NODE_FACTORY (Def_AST, DEF);


/********************   METHOD   ********************/

class Method_AST : public Def_AST {
protected:

    NODE_CONSTRUCTORS (Method_AST, Def_AST);

    Environ* setupEnviron () {
        return new Environ(curr_environ->get_enclosure());
    }

    bool _setupSelf(Decl* context) {
        if (getType()->numParams() == 0) {
            error(loc(), "instance methods must have at least one argument");
            return false;
        }

        int ar = context->getTypeArity();
        Type_Ptr* params = new Type_Ptr[ar];
        for (int i = 0; i < ar; i++) {
            params[i] = Type::makeVar();
        }

        Type_Ptr classType = context->asType(ar, params);
        child(1)->child(0)->getType()->unify(classType, global_bindings);
        return true;
    }
};

NODE_FACTORY (Method_AST, METHOD);


/********************   CLASS   ********************/
class Class_AST : public Region_AST {
protected:

    NODE_CONSTRUCTORS (Class_AST, Region_AST);

    Type_Ptr computeType() {
        return getDecl()->asType();
    }

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
        rewriteInit();
        curr_environ = prev_environ;
        return this;
    }

    /** Helper method to add an init method to a class if it does not
     *  exist */
    void rewriteInit () {
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
        gcstring text = child(0)->as_token()->as_string();
        Decl* decl = enclosing->getEnviron()->find(text);

        if (decl == NULL) {
            Decl* decl = makeClassDecl(text , child(1));
            enclosing->addMember(decl);
            curr_environ->define(decl);
            addDecl(decl);
        } else if (decl->isType()) {
            error(loc(), "syntax error: %s is already a class", text.c_str());
            exit(1);
        } else if (decl->isFunction()) {
            error(loc(), "syntax error: %s is already a definition",
                    text.c_str());
            exit(1);
        } else {
            error(loc(), "syntax error: %s is already a variable", text.c_str());
            exit(1);
        }
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

    AST_Ptr resolveTypes (Decl* context, int& resolved,
                          int& ambiguities, bool& errors) {
        Decl* decl = getDecl();
        for_each_child_var (c, this) {
            c = c->resolveTypes (decl, resolved, ambiguities, errors);
        } end_for;
        resolved++;
        return this;
    }
};

NODE_FACTORY (Class_AST, CLASS);


/********************   FORMALS_LIST   ********************/
class Formal_List_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Formal_List_AST, AST_Tree);

    /** Override AST::collectDecls to specify that children are
     *  parameters. */
    void collectDecls (Decl* enclosing) {
        for_each_child (c, this) {
            c->addParamDecls(enclosing, c_i_);
        } end_for;
    }
};

NODE_FACTORY (Formal_List_AST, FORMALS_LIST);


/********************   BLOCK  ********************/
class Block_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Block_AST, AST_Tree);

    /** By default, recursively call doInnerSemantics on each child. */
    AST_Ptr doInnerSemantics () {
        for_each_child_var (c, this) {
            c = c->doInnerSemantics ();
        } end_for;
        return this;
    }
};

NODE_FACTORY (Block_AST, BLOCK);


/********************   CLASS_BLOCK   ********************/
class Class_Block_AST : public Block_AST {
protected:

    NODE_CONSTRUCTORS (Class_Block_AST, Block_AST);
};

NODE_FACTORY (Class_Block_AST, CLASS_BLOCK);
