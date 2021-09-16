
#pragma once

#include "dust/widgets/textarea.h"

#include <cstring>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

namespace
{
    using namespace dust;
    
    // Minimal syntax highlighting for C-like languages
    //
    // NOTE: This is minimalistic by design, since I like it that way.
    struct SyntaxC : SyntaxParser
    {
        static bool wantFileType(const std::string & path)
        {
            const char * ext = strrchr(path.c_str(), '.');
            if(!ext) return false;

            if(!strcasecmp(ext, ".c")) return true;
            if(!strcasecmp(ext, ".h")) return true;
            if(!strcasecmp(ext, ".cpp")) return true;
            if(!strcasecmp(ext, ".cc")) return true;
            if(!strcasecmp(ext, ".cs")) return true;
            if(!strcasecmp(ext, ".js")) return true;
            if(!strcasecmp(ext, ".html")) return true;
            if(!strcasecmp(ext, ".java")) return true;
            if(!strcasecmp(ext, ".m")) return true;
            if(!strcasecmp(ext, ".mm")) return true;

            return false;
        }

        void (*cbFn)(void*ptr,TextAttrib*a);
        void *cbPtr;
    
        enum State
        {
            noState,
    
            // comments
            commentMaybe,   // lookahead for /* or //
            commentLine,    // waiting for \n
            commentBlock,   // waiting for */
            commentBlock1,  // waiting for / after *

            // #pragma
            inPragma,       // after #
            inPragmaOp,     // after # and some non-space
            inPragmaSpace,  // space after first non-space
            inPragmaInc,    // hopefully <include>
            
            // strings
            inString,
            inStringEscape, // go back to inString after next char
    
            // character literals
            inChar,
            inCharEscape,   // go back to inChar after next char
    
            errorState
        } state;
    
        bool inOper;    // don't bother with state for these
    
        void start(void (*set)(void*ptr,TextAttrib*a), void*ptr)
        {
            cbFn = set;
            cbPtr = ptr;
    
            state = noState;
            inOper = false;
        }
    
        void output(unsigned pos, unsigned attrib)
        {
            TextAttrib ta = { pos, attrib };
            cbFn(cbPtr, &ta);
        }
    
        void parse(unsigned pos, unsigned ch)
        {
            switch(state)
            {
                case inPragmaInc:
                    if(ch == '>')
                    {
                        output(pos+1, TextAttrib::aDefault);
                        state = noState;
                    }
                    if(ch == '\n') state = noState;
                    break;

                case inPragma:
                    if(ch == ' ' || ch == '\t') break;
                    state = inPragmaOp;
                    // fall thru
                case inPragmaOp:
                    if(ch == ' ' || ch == '\t')
                    {
                        state = inPragmaSpace;
                    }
                    if(ch == '\n')
                    {
                        output(pos, TextAttrib::aDefault);
                        state = noState;
                    }
                    break;
                    
                case inPragmaSpace:
                    if(ch == '<')
                    {
                        output(pos, TextAttrib::aLiteral);
                        state = inPragmaInc;
                        break;
                    }
                    if(ch == ' ' || ch == '\t') break;
                    
                    // fall thru
                    output(pos, TextAttrib::aDefault);
                    state = noState;

                case commentMaybe:
                    if(state == commentMaybe)
                    {
                        state = noState;
                        if(ch == '/') state = commentLine;
                        if(ch == '*') state = commentBlock;
                        if(state != noState)
                        {
                            output(pos-1, TextAttrib::aComment);
                            break;
                        }
                    }
                    // else fall-thru
                    
                case noState:
                    if(ch == '#')
                    {
                        output(pos, TextAttrib::aOperator);
                        state = inPragma;
                        break;
                    }
                    if(ch == '\n')
                    {
                        output(pos, TextAttrib::aDefault);
                    }
                    if(ch == '/') state = commentMaybe;
                    if(ch == '"')
                    {
                        state = inString;
                        output(pos, TextAttrib::aLiteral);
                        inOper = false;
                    }
                    if(ch == '\'')
                    {
                        state = inChar;
                        output(pos, TextAttrib::aLiteral);
                        inOper = false;
                    }
                    // color operator characters without state
                    // sanity check that we're looking at ascii
                    if(ch < 0x80 && strchr("+-*/%^&~|<>:.,;=!", ch))
                    {
                        if(inOper) break;
                        output(pos, TextAttrib::aOperator);
                        inOper = true;
                        break;
                    }
                    if(inOper)
                    {
                        output(pos, TextAttrib::aDefault);
                        inOper = false;
                    }
                    break;
    
                case commentLine:
                    if(ch == '\n')
                    {
                        state = noState;
                        output(pos, TextAttrib::aDefault);
                    }
                    break;
                case commentBlock1:
                    state = commentBlock;
                    if(ch == '/')
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                        break;
                    }
                    // else fall-thru
                case commentBlock:
                    if(ch == '*') state = commentBlock1;
                    break;
    
                case inString:
                    if(ch == '\\') state = inStringEscape;
                    // reset string/char on (invalid) newline
                    // this allows rest of the file to parse
                    // sensibly after incomplete literals
                    if(ch == '"' || ch == '\n')
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                    }
                    break;
                case inStringEscape:
                    state = inString;
                    if(ch == '\n') // see inString
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                    }
                    break;
    
                case inChar:
                    if(ch == '\\') state = inCharEscape;
                    if(ch == '\'' || ch == '\n') // see inString
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                    }
                    break;
                case inCharEscape:
                    state = inChar;
                    if(ch == '\n') // see inString
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                    }
                    break;
    
                default:
                    assert(false);
            }
        }
    
        virtual void flush()
        {
            // we don't need to do anything here
        }
    };

    // minimal syntax parser for your average scripting language
    struct SyntaxScript : SyntaxParser
    {
        static bool wantFileType(const std::string & path)
        {
            const char * base = strrchr(path.c_str(), '/');
            if(base && !strcmp(base, "/Makefile")) return true;
            
            const char * ext = strrchr(path.c_str(), '.');
            if(!ext) return false;

            if(!strcasecmp(ext, ".py")) return true;
            if(!strcasecmp(ext, ".sh")) return true;

            return false;
        }

        void (*cbFn)(void*ptr,TextAttrib*a);
        void *cbPtr;
    
        enum State
        {
            noState,
    
            // comments
            commentLine,    // waiting for \n
    
            // strings
            inString,
            inStringEscape, // go back to inString after next char
    
            // character literals
            inChar,
            inCharEscape,   // go back to inChar after next char
    
            errorState
        } state;
    
        bool inOper;    // don't bother with state for these
    
        void start(void (*set)(void*ptr,TextAttrib*a), void*ptr)
        {
            cbFn = set;
            cbPtr = ptr;
    
            state = noState;
            inOper = false;
        }
    
        void output(unsigned pos, unsigned attrib)
        {
            TextAttrib ta = { pos, attrib };
            cbFn(cbPtr, &ta);
        }
    
        void parse(unsigned pos, unsigned ch)
        {
            switch(state)
            {
                case noState:
                    if(ch == '#')
                    {
                        state = commentLine;
                        output(pos, TextAttrib::aComment);
                    }
                    if(ch == '"')
                    {
                        state = inString;
                        output(pos, TextAttrib::aLiteral);
                        inOper = false;
                    }
                    if(ch == '\'')
                    {
                        state = inChar;
                        output(pos, TextAttrib::aLiteral);
                        inOper = false;
                    }
                    // color operator characters without state
                    // sanity check that we're looking at ascii
                    if(ch < 0x80 && strchr("+-*/%^&~|<>:.,;=!", ch))
                    {
                        if(inOper) break;
                        output(pos, TextAttrib::aOperator);
                        inOper = true;
                        break;
                    }
                    if(inOper)
                    {
                        output(pos, TextAttrib::aDefault);
                        inOper = false;
                    }
                    break;
                case commentLine:
                    if(ch == '\n')
                    {
                        state = noState;
                        output(pos, TextAttrib::aDefault);
                    }
                    break;
                case inString:
                    if(ch == '\\') state = inStringEscape;
                    // reset string/char on (invalid) newline
                    // this allows rest of the file to parse
                    // sensibly after incomplete literals
                    if(ch == '"' || ch == '\n')
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                    }
                    break;
                case inStringEscape:
                    state = inString;
                    if(ch == '\n') // see inString
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                    }
                    break;
    
                case inChar:
                    if(ch == '\\') state = inCharEscape;
                    if(ch == '\'' || ch == '\n') // see inString
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                    }
                    break;
                case inCharEscape:
                    state = inChar;
                    if(ch == '\n') // see inString
                    {
                        state = noState;
                        output(pos+1, TextAttrib::aDefault);
                    }
                    break;
    
                default:
                    assert(false);
            }
        }
    
        virtual void flush()
        {
            // we don't need to do anything here
        }
    };
    
}