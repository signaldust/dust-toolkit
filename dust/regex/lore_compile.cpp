/****************************************************************************\
* Lore - regex library (c) Copyright pihlaja@signaldust.com 2014-2020        *
*----------------------------------------------------------------------------*
* You can use and/or redistribute this for whatever purpose, free of charge, *
* provided that the above copyright notice and this permission notice appear *
* in all copies of the software or it's associated documentation.            *
*                                                                            *
* THIS SOFTWARE IS PROVIDED "AS-IS" WITHOUT ANY WARRANTY. USE AT YOUR OWN    *
* RISK. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE HELD LIABLE FOR ANYTHING.  *
\****************************************************************************/

#include "lore.h"

#include <cassert>

// define to dump debug info
// this is just for development and not thread or stack safe
#undef DEBUG_LORE_COMPILE

#ifdef DEBUG_LORE_COMPILE
#include <dust/core/defs.h>
#endif

namespace lore
{

    // This is basically a fairly standard shift-reduce parser,
    // except sub-groups are handled recursively.
    //
    // For each token, push it to the stack with ++seq
    // Repedustion modifiers replace the top of the stack.
    // For alternation | operator, merge for seq=1 and push with ++alt
    // 
    // For a new sub-group, store and reset seq/alt, for end of group
    // pop alternatives, restore seq/alt and push as regular token.
    //
    // Since we need to know both entry and exit nodes for sub-machines
    // we keep "fragments" in stack and have patch_target() that knows
    // how to rewrite exit-node targets.

    struct Fragment
    {
        unsigned entry, exit;

        Fragment(unsigned entry, unsigned exit)
            : entry(entry), exit(exit) {}
        Fragment(const Fragment & f)
            : entry(f.entry), exit(f.exit) {}
    };

    // pack compiler state into a struct so we can use
    // helper functions to make this a bit more sane
    struct CompileState
    {
        unsigned nstates;   // parsed states
        
        // pointer to regex state vector
        std::vector<StateNode> * states;

        // pointer to regex cdata vector
        std::vector<ClassType> * cdata;

        // compile time stack, can store indexes
        std::vector<Fragment>   stack;

        // sub-group numbering
        unsigned nextSub;

        // escape character
        char escapeChar;

        // pattern data
        const char * pattern;
        // input position / length
        unsigned inpos, inlen;

        const char * error;

        // helpers
        CharType get() { return pattern[inpos++]; }
        CharType peek() { return pattern[inpos]; }

        bool eof() { return inpos == inlen; }
    };

    static void patch_target(CompileState & c, unsigned from, unsigned to)
    {
        StateNode & s = c.states->at(from);
        switch(s.tag)
        {
        case STATE_CHAR:
            s.ch.next = to;
            break;
        case STATE_CLASS:
        case STATE_NCLASS:
            s.cdata.next = to;
            break;
        case STATE_FUNC:
            s.func.next = to;
            break;
        case STATE_SAVE:
            s.save.next = to;
            break;
        case STATE_EMPTY:
            s.empty.next = to;
            break;
        default:
            c.error = "Internal error in patch_target.";
            return;
        }
    }

    static void reduce_opt(CompileState & c, bool greedy)
    {
        // pop the top of stack
        Fragment top(c.stack.back()); c.stack.pop_back();

        // we create two new nodes, one for split, one for merge
        c.stack.push_back(Fragment(c.states->size(),c.states->size()+1));

        // push the split node
        c.states->push_back(StateNode());
        StateNode & ns = c.states->back();

        ns.tag = STATE_SPLIT;
        ns.split.next0 = top.entry;
        ns.split.next1 = c.states->size();
        
        // greedy / non-greedy relies on order
        if(!greedy) std::swap(ns.split.next0, ns.split.next1);

        patch_target(c, top.exit, c.states->size());

        // push the merge node
        c.states->push_back(StateNode());
        StateNode & ne = c.states->back();

        ne.tag = STATE_EMPTY;
        ne.empty.next = ~0;
    }

    static void reduce_rep(CompileState & c, bool greedy)
    {
        // pop the top of stack
        Fragment top(c.stack.back()); c.stack.pop_back();

        // we create two nodes, but entry remains as-is
        c.stack.push_back(Fragment(top.entry,c.states->size()+1));

        // patch exit to the first new node
        patch_target(c, top.exit, c.states->size());

        // push the split node
        c.states->push_back(StateNode());
        StateNode & ns = c.states->back();

        ns.tag = STATE_SPLIT;
        ns.split.next0 = top.entry;
        ns.split.next1 = c.states->size();
        
        // greedy / non-greedy relies on order
        if(!greedy) std::swap(ns.split.next0, ns.split.next1);

        // push the merge node
        c.states->push_back(StateNode());
        StateNode & ne = c.states->back();

        ne.tag = STATE_EMPTY;
        ne.empty.next = ~0;
    }

    // this is optional repedustion, separate to avoid an extra merge
    static void reduce_rep_opt(CompileState & c, bool greedy)
    {
        // pop the top of stack
        Fragment top(c.stack.back()); c.stack.pop_back();

        // we create two new nodes, one for split, one for merge
        c.stack.push_back(Fragment(c.states->size(),c.states->size()+1));
        
        // patch exit back to the split
        patch_target(c, top.exit, c.states->size());

        // push the split node
        c.states->push_back(StateNode());
        StateNode & ns = c.states->back();

        ns.tag = STATE_SPLIT;
        ns.split.next0 = top.entry;
        ns.split.next1 = c.states->size();
        
        // greedy / non-greedy relies on order
        if(!greedy) std::swap(ns.split.next0, ns.split.next1);

        // push the merge node
        c.states->push_back(StateNode());
        StateNode & ne = c.states->back();

        ne.tag = STATE_EMPTY;
        ne.empty.next = ~0;
    }

    static void reduce_seq(CompileState & c, unsigned seq)
    {
        if(!seq)
        {
            // if there is no sequence, allow empty match
            c.stack.push_back(Fragment(c.states->size(), c.states->size()));
            c.states->push_back(StateNode());

            StateNode & n = c.states->back();
            n.tag = STATE_EMPTY;
            n.empty.next = c.states->size();
        }
        else
        {
            while(seq > 1)
            {
                // a.b
                Fragment b(c.stack.back()); c.stack.pop_back();
                Fragment a(c.stack.back()); c.stack.pop_back();

                patch_target(c, a.exit, b.entry);
                c.stack.push_back(Fragment(a.entry, b.exit));
                --seq;
            }
        }
    }

    static void reduce_all(CompileState & c,
        unsigned seq, unsigned alt, unsigned sub)
    {
        // first reduce sequence
        reduce_seq(c, seq);

        // for alt = 0, we just have a single case
        if(alt)
        {
            // create a single exit node, remember the index
            unsigned exit = c.states->size();
            {
                c.states->push_back(StateNode());
                StateNode & ne = c.states->back();
                ne.tag = STATE_EMPTY;
                ne.empty.next = ~0;
            }

            // pop the top of stack, patch exit
            Fragment b(c.stack.back()); c.stack.pop_back();
            patch_target(c, b.exit, exit);

            unsigned entry = b.entry;

            // we checked above, this runs at least once
            while(alt--)
            {
                // pop top of stack, patch exit
                Fragment a(c.stack.back()); c.stack.pop_back();
                patch_target(c, a.exit, exit);

                // push a split node
                c.states->push_back(StateNode());
                StateNode & ns = c.states->back();

                ns.tag = STATE_SPLIT;
                ns.split.next0 = a.entry;
                ns.split.next1 = entry;

                // the last one create is now the other
                entry = c.states->size() - 1;
            }

            // finally we have one alternative, push
            c.stack.push_back(Fragment(entry, exit));
        }

        // finally add sub-match data markers
        if(sub < 10)
        {
            Fragment a(c.stack.back()); c.stack.pop_back();
            // patch exit to first node
            patch_target(c, a.exit, c.states->size());

            // exit node
            c.states->push_back(StateNode());
            StateNode & ne = c.states->back();
            ne.tag = STATE_SAVE;
            ne.save.index = (sub<<1) + 1;
            ne.save.next = ~0;

            // start node
            c.states->push_back(StateNode());
            StateNode & ns = c.states->back();
            ns.tag = STATE_SAVE;
            ns.save.index = (sub<<1);
            ns.save.next = a.entry;

            // push the extended fragment
            c.stack.push_back(
                Fragment(c.states->size()-1,c.states->size()-2));
        }
    }

    static void match_func(CompileState & c, TestFunc tf)
    {
        c.stack.push_back(Fragment(c.states->size(),c.states->size()));

        c.states->push_back(StateNode());
        StateNode & n = c.states->back();

        n.tag = STATE_FUNC;
        n.func.func = tf;
        n.func.next = ~0;
    }

    static void match_char(CompileState & c, CharType ch)
    {
        c.stack.push_back(Fragment(c.states->size(),c.states->size()));
        
        c.states->push_back(StateNode());
        StateNode & n = c.states->back();

        n.tag = STATE_CHAR;
        n.ch.ch = ch;
        n.ch.next = ~0;
    }

    // parse an escape (always overwrites ch)
    //
    // returns true if this is a class -> sets tf
    // returns false if this is a character, found in ch
    // returns false and sets c.error() on error
    //
    static bool parse_escape_raw(CompileState & c, 
        CharType & ch, TestFunc & tf)
    {
        if(c.eof())
        {
            c.error = "Incomplete escape";
            return false;
        }

        ch = c.get();
        switch(ch)
        {
            // characters
        case 'e': ch = 27; return false;
        case 'n': ch = '\n'; return false;
        case 't': ch = '\t'; return false;
        case 'r': ch = '\r'; return false;
        case '0': ch = '\0'; return false;
        case '\\': ch = '\\'; return false;
            // functions
        case 'd': tf = TEST_DIGIT; return true;
        case 'D': tf = TEST_NOT_DIGIT; return true;
        case 's': tf = TEST_WHITE; return true;
        case 'S': tf = TEST_NOT_WHITE; return true;
        case 'w': tf = TEST_WORD; return true;
        case 'W': tf = TEST_NOT_WORD; return true;

        default:
            c.error = "Invalid escape";
            return false;
        }
    }

    static void parse_escape(CompileState & c)
    {
        CharType ch;
        TestFunc tf;
        if(parse_escape_raw(c, ch, tf))
        {
            match_func(c, tf);
        }
        else
        {
            if(c.error) return;
            match_char(c, ch);
        }
    }

    // this is bit of a mess, because a group can contain
    //  - arbitrary number of characters (possibly escaped)
    //  - arbitrary number of ranges
    //  - arbitrary number of test function (from escapes)
    //  - the whole range can be negated!
    //
    // See the comments on ClassType for packing.
    static void parse_group(CompileState & c)
    {
        // group parser: build as three vectors, then
        // merge into the native format afterwards
        std::vector<CharType>   chars;
        std::vector<CharType>   ranges;
        std::vector<TestFunc>   funcs;

        // check for negation
        bool negated = false;
        if(!c.eof() && c.peek() == '^')
        {
            negated = true;
            c.get();
        }

        while(true)
        {
            if(c.eof())
            {
                c.error = "Missing ]";
                return;
            }
            CharType ch = c.get();

            // first check group termination
            // this is because escapes can rewrite
            // ch into ] so we save special handling
            if(ch == ']')
            {
                // build cdata
                unsigned dataBegin = c.cdata->size();

                unsigned nchar = chars.size();
                unsigned nrange = ranges.size() / 2;
                unsigned nfunc = funcs.size();

                c.cdata->push_back(nchar);
                c.cdata->push_back(nrange);
                c.cdata->push_back(nfunc);

                for(unsigned i = 0; i < nchar; ++i)
                {
                    c.cdata->push_back(chars[i]);
                }
                for(unsigned i = 0; i < nrange; ++i)
                {
                    c.cdata->push_back(ranges[(i+i)]);
                    c.cdata->push_back(ranges[(i+i)+1]);
                }
                for(unsigned i = 0; i < nfunc; ++i)
                {
                    c.cdata->push_back(funcs[i]);
                }

                // ok.. construct node
                c.stack.push_back(Fragment(c.states->size(), c.states->size()));

                c.states->push_back(StateNode());
                StateNode & n = c.states->back();
                n.tag = negated ? STATE_NCLASS : STATE_CLASS;
                n.cdata.cdataIndex = dataBegin;
                n.cdata.next = ~0;
                return;
            }

            // we group didn't end, we must have at least
            // one more character, or we should fail now
            if(c.eof()) continue;

            if(ch == '-')
            {
                // disallow unescaped '-' completely
                // keeps the syntax rules cleaner
                c.error = "Invalid -";
                return;
            }

            if(ch == c.escapeChar)
            {
                TestFunc tf;
                if(parse_escape_raw(c, ch, tf))
                {
                    // function
                    funcs.push_back(tf);
                    // just continue
                    continue;
                }
                if(c.error) return;
            }

            // do we need this? well it won't hurt
            if(c.eof()) continue;

            // range?
            if(c.peek() == '-')
            {
                c.get();    // eat '-'

                // must have one character
                if(c.eof())
                {
                    c.error = "Invalid -";
                    return;
                }

                // get second character
                CharType ch2 = c.get();
                TestFunc tf;
                // process another escape if any
                if(ch2 == c.escapeChar && parse_escape_raw(c, ch2, tf))
                {
                    // if we end up here, we had function test
                    // in which case the range is invalid
                    c.error = "Invalid -";
                    return;
                }
                if(c.error) return;

                // ok, range from ch to ch2
                // allow inverted ranges
                if(ch2 < ch) std::swap(ch, ch2);

                // optimize single character ranges
                if(ch2 != ch)
                {
                    ranges.push_back(ch);
                    ranges.push_back(ch2);
                    continue;
                }
                // fall through otherwise
                // this was single character range
            }

            chars.push_back(ch);
        }

        // should not reach this
        c.error = "Internal error: parse_group";
        return;
    }

    // parse a (sub)-expression
    static void parse(CompileState & c, unsigned level)
    {
        // number of sequence and alternative tokens on stack
        unsigned seq = 0;
        unsigned alt = 0;

        // default to capture unless we change this
        bool capture = true;

        // check for (?:constructs) except on top-level
        if(level)
        {
            if(c.eof())
            {
                c.error = "Missing )";
                return;
            }
            if(c.peek() == '?')
            {
                c.get(); // eat '?'
                if(c.eof()) 
                {
                    c.error = "Missing )";
                }
                // get type 
                CharType ch = c.get();
                switch(ch)
                {
                case ':':
                    capture = false;
                    break;
                    // could support some other stuff
                default:    
                    // default to an error, since the (? ..) 
                    // constructs usually have drastic effects
                    c.error = "Invalid group specifier";
                    return;
                }
            }
        }

        // figure out the sub-group to use
        // for non-capturing just set "large"
        // which will keep the saves from being generated        
        unsigned sub = capture ? c.nextSub++ : ~0;

        // explicit exits
        while(true)
        {
            // error check
            if(c.eof()) 
            {
                if(level) 
                {
                    c.error = "Missing )";
                    return;
                }

                // finish
                reduce_all(c, seq, alt, sub);
                return;
            }
            
            CharType ch = c.get();

            if(ch == c.escapeChar)
            {
                parse_escape(c); ++seq;
                if(c.error) return;
                continue;
            }

            switch(ch)
            {
            case '?':
                if(!seq)
                {
                    c.error = "Unexpected ?";
                    return;
                }
                {
                    bool greedy = (c.peek() != '?');
                    if(!greedy) c.get();
                    reduce_opt(c, greedy);
                }
                break;
            case '+':
                if(!seq)
                {
                    c.error = "Unexpected +";
                    return;
                }
                {
                    bool greedy = (c.peek() != '?');
                    if(!greedy) c.get();
                    reduce_rep(c, greedy);
                }
                break;
            case '*':
                if(!seq)
                {
                    c.error = "Unexpected *";
                    return;
                }
                {
                    bool greedy = (c.peek() != '?');
                    if(!greedy) c.get();
                    reduce_rep_opt(c, greedy);  // optional repedustion
                }
                break;
            case '[':
                parse_group(c); ++seq;
                if(c.error) return;
                break;
            case ']':
                c.error = "Unexpected ]";
                return;
            case '|':   // reduce sequence to alternative
                reduce_seq(c, seq); seq = 0; ++alt;
                break;
            case '(':
                // parse a sub-expression
                parse(c, 1 + level); ++seq;
                if(c.error) return;
                break;
            case ')':
                if(!level) 
                {
                    c.error = "Unexpected )";
                    return;
                }
                // finish
                reduce_all(c, seq, alt, sub);
                return;
            case '.':
                match_func(c, TEST_NOT_CRLF); ++seq;
                break;
            default:
                match_char(c, ch); ++seq;
                break;
            }
        }
    }

    void Regex::compile(char escapeChar, const char * pattern, unsigned len)
    {
        CompileState c;

        // input
        c.pattern = pattern;
        c.inpos = 0;
        c.inlen = len;
        c.error = 0;
        c.nextSub = 0;
        c.states = &this->states;
        c.cdata = &this->cdata;
        c.escapeChar = escapeChar;

        // check anchors, save these too
        bool aBegin = (len && pattern[0] == '^');
        bool aEnd = (len && pattern[len-1] == '$');

        // check if the end-anchor is escaped
        // do this by scanning back and counting escapeChar
        if(aEnd)
        {
            int e = 0;
            for(int i = len; --i; ++e)
            {
                if(pattern[i-1] != escapeChar) break;
            }

            // if the count is odd, the anchor is escaped
            if(e & 1) aEnd = false;
        }

        hasBeginAnchor = aBegin;

        if(aBegin) 
        {
            ++c.inpos;  // patch position for better errors
        }
        else
        {
            // synthesize optional non-greedy repedustion
            // this eats anything before the actual match
            match_func(c, TEST_TRUE);
            reduce_rep_opt(c, false);
        }

        if(aEnd) --c.inlen;

        // call parser
        parse(c, 0);

        // set error data .. in case there was one
        errorString = c.error;
        // position of error is last character read
        errorPos = c.inpos - 1;

        assert(c.error 
            || c.stack.size() == (aBegin ? 1 : 2));
        
        if(!c.error)
        {
            if(aEnd)
            {
                // special EOF marker
                match_char(c, CharEOF);
                // sequence with the pattern
                reduce_seq(c, 2);
            }

            if(!aBegin)
            {
                // if we pushed the leading garbage loop
                // then sequence that with the pattern
                reduce_seq(c, 2);
            }

            // pop the finished machine
            Fragment a(c.stack.back()); c.stack.pop_back();

            // store the starting / ending nodes
            first = a.entry;
            patch_target(c, a.exit, c.states->size());

            // push the ending node
            c.states->push_back(StateNode());
            StateNode & n = c.states->back();
            n.tag = STATE_MATCH;
        }
        
        // done
#ifdef DEBUG_LORE_COMPILE
        if(c.error)
        {
            char buf[123];
            sprintf(buf, "Error in regex '%s' at %p: %s\n",
                pattern, c.pattern, c.error);
            dust::debugPrint("%s", buf);
            return;
        }
        {
            char buf[123];
            sprintf(buf, "Regex '%s' compiled, start from %d\n", 
                pattern, first);
            dust::debugPrint("%s", buf);
        }
        for(int i = 0; i < states.size(); ++i)
        {
            char buf[123];

            switch(states[i].tag)
            {
            case STATE_CHAR:
                sprintf(buf, " %d: char '%c' -> %d\n",
                    i, states[i].ch.ch, states[i].ch.next);
                break;
            case STATE_FUNC:
                sprintf(buf, " %d: func '%p' -> %d\n",
                    i, states[i].func.func, states[i].func.next);
                break;
            case STATE_SPLIT:
                sprintf(buf, " %d: split -> %d | %d\n",
                    i, states[i].split.next0, states[i].split.next1);
                break;
            case STATE_CLASS:
                sprintf(buf, " %d: class -> %d\n",
                    i, states[i].cdata.next);
                break;
            case STATE_NCLASS:
                sprintf(buf, " %d: not-class -> %d\n",
                    i, states[i].cdata.next);
                break;
            case STATE_EMPTY:
                sprintf(buf, " %d: empty -> %d\n",
                    i, states[i].empty.next);
                break;
            case STATE_SAVE:
                sprintf(buf, " %d: save %d -> %d\n",
                    i, states[i].save.index, states[i].save.next);
                break;
            case STATE_MATCH:
                sprintf(buf, " %d: accept\n", i);
                break;
            case STATE_COUNT: break; /* silence clang */
            }
            
            dust::debugPrint("%s", buf);
        }
#endif
    }

}; // namespace
