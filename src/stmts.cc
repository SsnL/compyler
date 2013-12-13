/* -*- mode: C++; c-file-style: "stroustrup"; indent-tabs-mode: nil; -*- */

/* stmts.cc: AST subclasses related to statements and modules. */

/* Authors:  YOUR NAMES HERE */

#include <iostream>
#include <sstream>
#include <set>
#include "apyc.h"
#include "ast.h"
#include "apyc-parser.hh"

using namespace std;

static GCINIT _gcdummy;

/***** PRINT *****/

/** A print statement with a trailing comma. */
class Print_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Print_AST, AST_Tree);

    AST_Ptr resolveTypes (Decl* context, int& resolved, int& ambiguities) {
        AST_Tree::resolveTypes (context, resolved, ambiguities);
        if (!child (0)->isMissing ()) {
            if (!child (0)->getType ()->unify (fileDecl->asType ())) {
                error (this, "target argument in print must be file");
            }
        }
        return this;
    }

    /* Prints a newline or not depending on the type of the print. */
    virtual void printEpilogue () {
        VM->comment("Adding space for print");
        VM->code("cout << \" \";");
    }

    virtual gcstring addon () {
        return "\"\"";
    }

    void stmtCodeGen (int depth) {
        child(1)->exprCodeGen(depth);
        if (child(0)->isMissing()) {
            VM->emit(PRNT, child(1)->arity());
        } else {
            child(0)->exprCodeGen(depth);
            VM->emit(PRNTFILE, addon (), child(1)->arity());
            return;
        }
        printEpilogue();
    }
};

NODE_FACTORY (Print_AST, PRINT);

/*****   PRINTLN   *****/

/** A print statement without trailing comma. */
class Println_AST : public Print_AST {
protected:

    NODE_CONSTRUCTORS (Println_AST, Print_AST);

    const char* externalName () {
    return "println";
    }

    gcstring addon () {
        return "\"\\n\"";
    }

    void printEpilogue() {
        VM->comment("Adding newline for println");
        VM->code("cout << endl;");
    }

};

NODE_FACTORY (Println_AST, PRINTLN);

/***** STMT_LIST *****/

/** A list of statements. */
class StmtList_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (StmtList_AST, AST_Tree);

    AST_Ptr doOuterSemantics () {
        for_each_child_var (c, this) {
            c = c->doOuterSemantics ();
        } end_for;
        return this;
    }

    void classCodeGen (int depth) {
        for_each_child (c, this) {
            c->classCodeGen (depth);
        } end_for;
    }

    void defCodeGen (int depth) {
        for_each_child (c, this) {
            c->defCodeGen (depth);
        } end_for;
    }

    void stmtCodeGen (int depth) {
        for_each_child (c, this) {
            c->stmtCodeGen (depth);
        } end_for;
    }

};

NODE_FACTORY (StmtList_AST, STMT_LIST);

class Block_AST : public StmtList_AST {

    NODE_CONSTRUCTORS (Block_AST, StmtList_AST);
};

NODE_FACTORY (Block_AST, BLOCK);

/***** DEF *****/

/** def ID(...): ... */
class Def_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Def_AST, AST_Tree);

    Decl* getDecl (int k = 0) {
        return child (0)->getDecl ();
    }

    void freezeDecls (bool frozen) {
        Decl* me = getDecl ();
        if (me != NULL)
            me->setFrozen (frozen);
        child (3)->freezeDecls (frozen);
    }

    void collectDecls (Decl* enclosing) {
        AST_Ptr id = child (0);
        AST_Ptr params = child (1);
        AST_Ptr returnType = child (2);
        AST_Ptr body = child (3);
        gcstring name = id->as_string ();
        if (undefinable (name)) {
            error (this, "may not define %s as a function", name.c_str ());
        }
        Decl* me = enclosing->addDefDecl(child(0), params->arity ());
        id->addDecl (me);
        params->collectDecls (me);
        params->collectTypeVarDecls (me);
        returnType->collectTypeVarDecls (me);
        body->collectDecls (me);
    }

    AST_Ptr resolveSimpleIds (const Environ* env) {
        child (3)->resolveSimpleIds (getDecl ()->getEnviron ());
        return this;
    }

    AST_Ptr convertNone (bool) {
        replace (3, child (3)->convertNone (false));
        return this;
    }

    AST_Ptr resolveTypes (Decl* context, int& resolved, int& ambiguities) {
        Decl* me = getDecl ();
        Type_Ptr funcType = me->getType ()->binding ();
        AST_Ptr returnType = child (2);
        AST_Ptr body = child (3);
        AST_Ptr formals = child (1);
        formals->resolveTypes (me, resolved, ambiguities);
        if (!returnType->isMissing ()) {
            if (!funcType->returnType ()->unify (returnType->asType ()))
                error (this, "inconsistent return type");
        }
        for (size_t i = 0; i < formals->arity (); i += 1) {
            if (!formals->child (i)->getType ()
                ->unify (funcType->paramType (i))) {
                error (this, "inconsistent parameter type");
            }
        }
        body->resolveTypes (me, resolved, ambiguities);
        assert ((int) me->getType ()->binding ()->numParams () ==
                (int) formals->arity ());
        return this;
    }

    void declDepthPreprocess (int& currDepth) {
        Decl* me = getDecl ();
        Decl_Vector members = me->getEnviron ()->get_members ();
        currDepth++;
        for (int i = 0; i < members.size (); i++) {
            members[i]->setDepth(currDepth);
        }
        child (3)->declDepthPreprocess(currDepth);
        currDepth--;
        me->setDepth(currDepth);
    }

    void declNamePreprocess (gcmap<gcstring, int>& names) {
        Decl* me = getDecl ();
        Decl_Vector members = me->getEnviron ()->get_members ();
        for (int i = 0; i < members.size (); i++)
            members[i]->setupRuntimeName (names);
        child (3)->declNamePreprocess (names);
    }

    void containerPreprocess (Decl* container) {
        setContainer (container);
        for_each_child (c, this) {
            c->containerPreprocess (getDecl ());
        } end_for;
    };

    /** Generates the runtime data structures for myself and prints them on
     *  OUT. Creates a templated C struct depending on whether or not there are
     *  any unbound type variables in me. */
    void runtimeDataStructGen (std::ostream& out) {
        Decl* me = getDecl ();
        Decl_Vector members = me->getEnviron ()->get_members ();
        Decl* memberDecl;
        gcstring memberName, memberTypeName = "$Reference";
        stringstream body;
        for (int i = 0; i < members.size (); i++) {
            memberDecl = members[i];
            memberName = memberDecl->getRuntimeName ();
            if (memberTypeName != "")
                body << "    "
                     << memberTypeName << " " << memberName << ";" << endl;
        }
        out << "struct " << me->getRuntimeName () << " {" << endl
            << body.str ()
            << "};" << endl << endl;
        AST::runtimeDataStructGen (out);
    }

    void defCodeGen (int depth) {
        gcstring defname = getDecl ()->getRuntimeName ();
        VMLabel lbl = VM->asLabel (defname);
        // start with my definition
        VM->placeLabel (lbl);
        VM->emitDefPrologue (defname);
        // bind params to variables
        if (!getDecl ()->isNative ()) {
            for_each_child (c, child (1)) {
                c->exprCodeGen (depth + 1);
                VM->emit (MOVE);
                VM->emit (POP);
            } end_for;
        }
        // cgen main body
        child (3)->stmtCodeGen (depth + 1);
        VM->newline();
        VM->comment("returning None by default");
        VM->emit(NTV, "__None__", 0);
        VM->emitDefEpilogue (defname);
        // recursively cgen any nested definitions
        child (3)->defCodeGen (depth + 1);
    }

    void stmtCodeGen (int depth) {
        gcstring defname = getDecl ()->getRuntimeName ();
        gcstring frameName;
        frameName = getDecl ()->getContainer ()->getRuntimeName ();

        VM->emit (ALLOC, "new FuncDesc");
        VM->emit (PUSH);
        if (getDecl ()->getContainer ()->isType ())
            VM->emit (PUSH, "$" + frameName + "." + defname);
        else
            VM->emit (PUSH, "((" + frameName + "*) cf->locals)->" + defname);
        VM->emit (MOVE);
        VM->emit (SETLBL, VM->asLabel (defname));
        VM->emit (SETSL, "cf");
        VM->emit (POP);
    }

};

NODE_FACTORY (Def_AST, DEF);

/***** METHOD *****/

/**  def ID(...): ... (in class)     */
class Method_AST : public Def_AST {
protected:

    NODE_CONSTRUCTORS (Method_AST, Def_AST);

    void collectDecls (Decl* enclosing) {
        AST_Ptr params = child (1);
        if (params->arity () == 0) {
            error (this, "method must have at least one parameter");
            params->insert (0, make_id ("__self__", loc ()));
        }
        Def_AST::collectDecls (enclosing);
    }


};

NODE_FACTORY (Method_AST, METHOD);

/***** NATIVE *****/
class Native_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Native_AST, AST_Tree);

    void stmtCodeGen (int depth) {
        int argc = getContainer ()->getType ()->numParams();
        VM->emit(NTV, child (0)->as_string (), argc);
        VM->emitDefEpilogue( getContainer ()->getRuntimeName ());
    }

    void containerPreprocess (Decl* container) {
        container->setNative(true);
        AST::containerPreprocess (container);
    }

};

NODE_FACTORY (Native_AST, NATIVE);


/***** FORMALS_LIST *****/

/** ... (E1, ...)  */
class FormalsList_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (FormalsList_AST, AST_Tree);

    void collectDecls (Decl* enclosing) {
        for_each_child (c, this) {
            AST_Ptr id = c->getId ();
            gcstring name = id->as_string ();
            if (undefinable (name))
                error (this, "may not use %s as a parameter", name.c_str ());
            c->addDecl (enclosing->addParamDecl(id, c_i_));
        } end_for;

        for_each_child (c, this) {
            c->collectTypeVarDecls (enclosing);
        } end_for;
    }

};

NODE_FACTORY (FormalsList_AST, FORMALS_LIST);

/***** CLASS *****/

/** class ID of [...]: ...  */
class Class_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Class_AST, AST_Tree);

    Decl* getDecl (int k = 0) {
        return child (0)->getDecl ();
    }

    void collectDecls (Decl* enclosing) {
        AST_Ptr id = child (0);
        gcstring name = id->as_string ();
        AST_Ptr params = child (1);
        AST_Ptr body = child (2);
        const Environ* env = enclosing->getEnviron ();

        if (undefinable (name)) {
            error (this, "may not define %s as a function", name.c_str ());
        }

        Decl* me = makeClassDecl (name, params);

        if (env->find_immediate (name) != NULL) {
            error (id, "attempt to redefine %s", name.c_str ());
        } else {
            enclosing->addMember (me);
        }

        setBuiltinDecl (me);

        id->addDecl (me);
        params->collectDecls (me);
        body->collectDecls (me);

        if (me->getEnviron ()->find_immediate ("__init__") == NULL)
            addInit ();
    }

    AST_Ptr resolveSimpleIds (const Environ* env) {
        child (2)->resolveSimpleIds (getDecl ()->getEnviron ());
        return this;
    }

    AST_Ptr convertNone (bool) {
        replace (2, child (2)->convertNone (false));
        return this;
    }

    AST_Ptr resolveTypes (Decl* context, int& resolved, int& ambiguities) {
        replace (2, child (2)->resolveTypes (getDecl (), resolved, ambiguities));
        return this;
    }

    void declDepthPreprocess (int& currDepth) {
        Decl* me = getDecl ();
        Decl_Vector members = me->getEnviron ()->get_members ();
        for (int i = 0; i < members.size (); i++)
            members[i]->setDepth (currDepth);
        me->setDepth(currDepth);
        child (2)->declDepthPreprocess(currDepth);
    }

    void declNamePreprocess (gcmap<gcstring, int>& names) {
        Decl* me = getDecl ();
        Decl_Vector members = me->getEnviron ()->get_members ();
        for (int i = 0; i < members.size (); i++) {
            members[i]->setupRuntimeName (names);
        }
        child (2)->declNamePreprocess (names);
    }

    void runtimeDataStructGen (std::ostream& out) {
        if (isPrimitive(getDecl ()))
            return;
        Decl* me = getDecl ();
        Decl_Vector members = me->getEnviron ()->get_members ();
        Decl* memberDecl;
        gcstring memberName, memberTypeName = "$Reference";
        stringstream body;
        for (int i = 0; i < members.size (); i++) {
            memberDecl = members[i];
            memberName = memberDecl->getRuntimeName ();
            if (memberTypeName != "") {
                body << "    "
                     << memberTypeName << " " << memberName << ";" << endl;
            }
        }
        out << "class " << me->getRuntimeName() << " : public $Object {" << endl
            << "public:" << endl << endl
            << body.str () << endl
            << "};" << endl
            <<  me->getRuntimeName () << " $" << me->getRuntimeName () << ";"
            << endl << endl;
        AST::runtimeDataStructGen (out);
    }

    void stmtCodeGen (int depth) {
        if (!isPrimitive(getDecl ())) {
            for_each_child (c, child (2)) {
                c->stmtCodeGen (depth);
            } end_for;
        }
    }

    void defCodeGen (int depth) {
        if (!isPrimitive(getDecl ())) {
            for_each_child (c, child (2)) {
                c->defCodeGen (depth);
            } end_for;
        }
    }

private:
    /** Add declaration of method init to me. */
    void addInit () {
        Decl* me = getDecl (0);
        AST_Ptr newDef =
            consTree (METHOD, make_id ("__init__", loc ()),
                      consTree (FORMALS_LIST, make_id ("self", loc ())),
                      consTree (EMPTY),
                      consTree (BLOCK));
        newDef->collectDecls (me);
        child (2)->insert (0, newDef);
    }

    /** Check if declaration is a primitive type. */
    bool isPrimitive (Decl* decl) {
        return decl == intDecl
            || decl == listDecl
            || decl == tupleDecl[0]
            || decl == tupleDecl[1]
            || decl == tupleDecl[2]
            || decl == tupleDecl[3]
            || decl == strDecl
            || decl == dictDecl
            || decl == boolDecl
            || decl == fileDecl
            || decl == rangeDecl;
    }
};

NODE_FACTORY (Class_AST, CLASS);

/***** TYPE_FORMALS_LIST *****/

/** of [$T, ...]      */
class TypeFormalsList_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (TypeFormalsList_AST, AST_Tree);

    void collectDecls (Decl* enclosing) {
        const Environ* env = enclosing->getEnviron ();
        for_each_child (c, this) {
            gcstring name = c->as_string ();
            if (env->find_immediate (name) != NULL) {
                error (c, "duplicate type parameter: %s",
                       name.c_str ());
            } else {
                c->addDecl (enclosing->addTypeVarDecl (c));
            }
        } end_for;
    }

};

NODE_FACTORY (TypeFormalsList_AST, TYPE_FORMALS_LIST);

/***** TYPED_ID *****/

/** ID::TYPE */
class TypedId_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (TypedId_AST, AST_Tree);

    AST_Ptr getId () {
        return child (0);
    }

    Decl* getDecl (int k = 0) {
        return getId ()->getDecl (k);
    }

    Type_Ptr getType () {
        return child (1)->asType ();
    }

    void addTargetDecls (Decl* enclosing) {
        getId ()->addTargetDecls (enclosing);
    }

    void collectTypeVarDecls (Decl* enclosing) {
        child (1)->collectTypeVarDecls (enclosing);
    }

    void addDecl (Decl* decl) {
        getId ()->addDecl (decl);
    }


    AST_Ptr resolveTypes (Decl* context, int& resolved, int& ambiguities) {
        getId ()->resolveTypes (context, resolved, ambiguities);
        if (!getId ()->setType (child (1)->asType ()))
            error (this, "incompatible type assignment");
        return this;
    }

    void classCodeGen (int depth) {
        getId ()->classCodeGen (depth);
    }

    void exprCodeGen (int depth) {
        getId ()->exprCodeGen (depth);
    }

};

NODE_FACTORY (TypedId_AST, TYPED_ID);


/***** ASSIGN *****/

/** TARGET(s) = EXPR */
class Assign_AST : public Typed_Tree {
protected:

    NODE_CONSTRUCTORS (Assign_AST, Typed_Tree);

    void collectDecls (Decl* enclosing) {
        child(1)->collectDecls (enclosing);
        child(0)->addTargetDecls (enclosing);
    }

    AST_Ptr resolveTypes (Decl* context, int& resolved, int& ambiguities) {
        int errs0 = numErrors ();
        AST_Tree::resolveTypes (context, resolved, ambiguities);
        if (!child (0)->getType ()->unify (child (1)->getType ())
            && errs0 == numErrors ())
            error (this, "type mismatch in assignment");
        if (!setType (child (1)->getType ()) && errs0 == numErrors ())
            error (this, "type mismatch in assignment");
        return this;
    }

    AST_Ptr convertNone (bool) {
        child (0)->convertNone (true);
        replace (1, child (1)->convertNone (false));
        return this;
    }

    void stmtCodeGen (int depth) {
        exprCodeGen (depth);
        VM->emit (POP);
    }

    void exprCodeGen (int depth) {
        if (child (0)->isTargetList ()) {
            int tuplesize = child (0)->arity ();
            child (1)->exprCodeGen (depth);
            VM->emit (EXPAND, tuplesize);
            for_each_child (c, child (0)) {
                c->exprCodeGen (depth);
                VM->emit (MOVE);
                VM->emit (POP);
            } end_for;
            child (0)->exprCodeGen (depth);
        } else {
            child (1)->exprCodeGen (depth);
            child (0)->exprCodeGen (depth);
            VM->emit (MOVE);
        }
    }
};

NODE_FACTORY (Assign_AST, ASSIGN);


/***** If *****/

class If_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (If_AST, AST_Tree);

    void stmtCodeGen (int depth) {

        VMLabel elseLbl = VM->newLabel ("ELSE");
        VMLabel exitLbl = VM->newLabel ("EXIT");

        child (0)->exprCodeGen (depth);
        VM->emit (GTZ, elseLbl);
        child (1)->stmtCodeGen (depth);
        VM->emit (GOTO, exitLbl);
        VM->placeLabel (elseLbl);
        if (arity() > 2) {
            child (2)->stmtCodeGen (depth);
        } else {
            VM->newline ();
        }
        VM->placeLabel (exitLbl);
    }
};

NODE_FACTORY (If_AST, IF);


/***** FOR *****/

/**  for target in exprs: body [ else: body ]     */
class For_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (For_AST, AST_Tree);

    AST_Ptr resolveTypes (Decl* context, int& resolved, int& ambiguities) {
        int errs0 = numErrors ();
        AST_Tree::resolveTypes (context, resolved, ambiguities);
        if (errs0 != numErrors ())
            return this;
        Type_Ptr seqType = child (1)->getType ()->binding ();

        if (seqType->isTypeVariable ()) {
            ambiguities += 1;
            return this;
        }

        Type_Ptr eltType;
        if (seqType->unifies (strDecl->asType ()))
            eltType = strDecl->asType ();
        else if (seqType->unifies (rangeDecl->asType ()))
            eltType = intDecl->asType ();
        else {
            eltType = Type::makeVar ();
            if (!seqType->unify (listDecl->asType (1, eltType)))
                error (this, "value cannot be iterated over");
        }
        if (!eltType->unify (child (0)->getType ())) {
            error (this, "for loop target does not match element type");
        }

        return this;
    }

    void collectDecls (Decl* enclosing) {
        AST_Ptr target = child (0);
        for (size_t i = 1; i < arity (); i += 1)
            child (i)->collectDecls (enclosing);
        target->addTargetDecls (enclosing);
    }

};

NODE_FACTORY (For_AST, FOR);


/***** FOR *****/

/**  for target in exprs: body [ else: body ]     */
class While_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (While_AST, AST_Tree);

    void stmtCodeGen (int depth) {

        stringstream ss;
        VMLabel elseLbl = VM->newLabel ("ELSE");
        VMLabel loopLbl = VM->newLabel ("LOOP");
        VMLabel exitLbl = VM->newLabel ("EXIT");
        ss << "&&" << exitLbl << ";";

        VM->emit (PUSH, ss.str ());
        child (0)->stmtCodeGen (depth);
        VM->emit (GTZ, elseLbl);
        // main loop
        VM->placeLabel (loopLbl);
        child (1)->stmtCodeGen (depth);
        child (0)->stmtCodeGen (depth);
        VM->emit (GTZ, elseLbl);
        VM->emit (GOTO, loopLbl);
        // else and exit
        VM->placeLabel (elseLbl);
        child (2)->stmtCodeGen (depth);
        // pop the label if break not used
        VM->emit (POP);
        VM->placeLabel (exitLbl);
    }
};

NODE_FACTORY (While_AST, WHILE);


/***** RETURN *****/

/**  return EXPR */
class Return_AST : public AST_Tree {
protected:

    NODE_CONSTRUCTORS (Return_AST, AST_Tree);

    AST_Ptr resolveTypes (Decl* context, int& resolved, int& ambiguities) {
        AST_Tree::resolveTypes (context, resolved, ambiguities);
        AST_Ptr expr = child (0);
        if (! expr->isMissing ()) {
            Type_Ptr returnType = context->getType ()->returnType ();
            if (!returnType->unify (expr->getType ()))
                error (this, "inconsistent return type");
        }
        return this;
    }

    void exprCodeGen (int depth) {
        // Push the return value, if it exists, onto stack.
        VM->comment("Returning from this function");
        if (!(child (0)->isMissing ())) {
            VM->newline();
            VM->comment("Pushing return value onto stack machine");
            child (0)->exprCodeGen (depth);
        } else {
            VM->newline();
            VM->comment("returning None by default");
            VM->emit(NTV, "__None__", 0);
        }

        // Return to where this function is called.
        VM->emitDefEpilogue (getContainer ()->getRuntimeName ());
    }

    void stmtCodeGen (int depth) {
        exprCodeGen (depth);
    }

};

NODE_FACTORY (Return_AST, RETURN);

