/* -*- mode: C++; c-file-style: "stroustrup"; indent-tabs-mode: nil; -*- */

/* tokens.cc: Definitions related to AST_Token and its subclasses. */

/* Authors:  YOUR NAMES HERE */

#include <iostream>
#include <cstdlib>
#include "apyc.h"
#include "ast.h"
#include "apyc-parser.hh"

using namespace std;

/** Default print for tokens. */
void
AST_Token::print (ostream& out, int indent)
{
    out << "(<Token>)";
}

/** Default implementation. */
string
AST_Token::string_text () const
{
    throw logic_error ("unimplemented operation: string_text");
}

/** Default implementation. */
void
AST_Token::append_text(const string& s)
{
    throw logic_error ("unimplemented operation: append_text");
}

/** Represents an type vairable (ie. $MyType, from x::$MyType) */
class TypeVar_Token : public AST_Token {
private:
    void print (ostream& out, int indent) {
        out << "(type_var " << lineNumber() << " "
            << string(as_chars(), text_size()).c_str() << ")";
    }

    TOKEN_CONSTRUCTORS(TypeVar_Token, AST_Token);
};

TOKEN_FACTORY(TypeVar_Token, TYPE_VAR);

/** Represents an ID. */
class ID_Token : public AST_Token {
private:
    void print (ostream& out, int indent) {
        out << "(id " << lineNumber() << " "
            << string(as_chars(), text_size()).c_str() << ")";
    }

    TOKEN_CONSTRUCTORS(ID_Token, AST_Token);
};

TOKEN_FACTORY(ID_Token, ID);

/** Represents an integer literal. */
class Int_Token : public AST_Token {
private:

    void print (ostream& out, int indent) {
        out << "(int_literal " << lineNumber () << " " << value << ")";
    }

    /** Initialize value from the text of the lexeme, checking that
     *  the literal is in range.  [The post_make method may be
     *  overridden to provide additional processing during the
     *  construction of a node or token.] */
    Int_Token* post_make () {
        // The maximun int literal can be is 2^30
        int max = 1 << 30;

        const char* text = string(as_chars(), text_size()).c_str();
        int base;
        // Check if it is decimal, octal, or hex
        if (text_size() > 1 && text[0] == '0') {
            if (text_size() > 2 && (text[1] == 'x' || text[1] == 'X') ) {
                // prefix "0x" or "0X", it is hex
                base = 16;
            } else {
                // prefix "0", it is octal
                base = 8;
            }
        } else {
            // It is decimal
            base = 10;
        }

        // Converting string to numerial value based on the prefix
        // Prefix "0" parse to octal
        // Prefix "0x" or "0X" parse to hex
        // Otherwise parse to decimal
        value = strtol(string(as_chars(), text_size()).c_str(), NULL, base);
        if (value > max) {
          if (base == 10) {
            error (as_chars(), "Decimal integer overflow error");
          } else if (base == 16) {
            error (as_chars(), "Hexadecimal integer overflow error");
          } else {
            error (as_chars(), "Octal integer overflow error");
          }
        }
        return this;
    }

    long value;

    TOKEN_CONSTRUCTORS(Int_Token, AST_Token);
};

TOKEN_FACTORY(Int_Token, INT_LITERAL);


/** Represents a string. */
class String_Token : public AST_Token {
private:

    /** Set literal_text from the text of this lexeme, converting
     *  escape sequences as necessary. */
    String_Token* post_make () {
        if (syntax () == RAWSTRING) {
            literal_text = string (as_chars (), text_size ());
        } else {
            int v;
            const char* s = as_chars ();
            size_t i;
            i = 0;
            literal_text.clear ();
            while (i < text_size ()) {
                i += 1;
                if (s[i-1] == '\\') {
                    i += 1;
                    switch (s[i-1]) {
                    default: literal_text += '\\'; v = s[i-1]; break;
                    case '\n': continue;
                    case 'a': v = '\007'; break;
                    case 'b': v = '\b'; break;
                    case 'f': v = '\f'; break;
                    case 'n': v = '\n'; break;
                    case 'r': v = '\r'; break;
                    case 't': v = '\t'; break;
                    case 'v': v = '\v'; break;
                    case '\'': v = '\''; break;
                    case '"': case '\\': v = s[i-1]; break;
                    case '0': case '1': case '2': case '3': case '4':
                    case '5': case '6': case '7':
                    {
                        v = s[i-1] - '0';
                        for (int j = 0; j < 2; j += 1) {
                            if ('0' > s[i] || s[i] > '7')
                                break;
                            v = v*8 + (s[i] - '0');
                            i += 1;
                        }
                        break;
                    }
                    case 'x': {
                        if (i+2 > text_size () ||
                            !isxdigit (s[i]) || !isxdigit (s[i+1])) {
                            error (s, "bad hexadecimal escape sequence");
                            break;
                        }
                        sscanf (s+i, "%2x", &v);
                        i += 2;
                        break;
                    }
                    }
                } else
                    v = s[i-1];
                literal_text += (char) v;
            }
        }
        return this;
    }

    void print (ostream& out, int indent) {
        out << "(string_literal " << lineNumber () << " \"";
        for (size_t i = 0; i < literal_text.size (); i += 1) {
            char c = literal_text[i];
            if (c < 32 || c == '\\' || c == '"') {
                out << "\\" << oct << setw (3) << setfill('0') << (int) c
                    << setfill (' ') << dec;
            } else
                out << c;
        }
        out << "\")";
    }

    string string_text () const {
        return literal_text;
    }

    void append_text(const string& s) {
        literal_text += s;
    }

    TOKEN_CONSTRUCTORS(String_Token, AST_Token);
    static const String_Token raw_factory;

    string literal_text;
};

TOKEN_FACTORY(String_Token, STRING);

/** A dummy token whose creation registers String_Token as the class
 *  to use for RAWSTRING tokens produced by the lexer.  (The
 *  TOKEN_FACTORY macro above registers String_Token as the class for
 *  non-raw the STRING tokens as well.)
 *  */
const String_Token String_Token::raw_factory (RAWSTRING);
