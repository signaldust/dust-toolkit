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

// define to trace eval with debug prints
// this is just for development and not thread or stack safe
#undef DEBUG_LORE_EVAL

#ifdef DEBUG_LORE_EVAL
#include "dust/core/defs.h"
#endif

#ifdef DEBUG_LORE_EVAL
static unsigned reclevel = 0;
#endif

namespace lore
{
    static bool test_white(CharType ch)
    {
        // this probably should call "iswhite()" or something
        // but .. for now this is good enough
        return (ch == ' ' || ch == '\n' 
            || ch == '\t' || ch == '\r');
    }

    static bool test_digit(CharType ch)
    {
        return (ch >= '0' && ch <= '9');
    }

    static bool test_alnum(CharType ch)
    {
        return (ch >= 'A' && ch <= 'Z')
            || (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9');
    }

    static bool test_word(CharType ch)
    {
        return ch == '_' || ch > 0x80 || test_alnum(ch);
    }

    // test function for anything (except CR/LF)
    static bool test_not_crlf(CharType ch)
    {
        return (ch != '\n' && ch != '\r');
    }

    // this will match absolutely anything
    // used to eat stuff when there is not ^ anchor
    static bool test_true(CharType ch)
    {
        return true;
    }

    static bool call_test(TestFunc test, CharType ch)
    {
        switch(test)
        {
        case TEST_WHITE: return test_white(ch);
        case TEST_NOT_WHITE: return !test_white(ch);
        
        case TEST_DIGIT: return test_digit(ch);
        case TEST_NOT_DIGIT: return !test_digit(ch);
        
        case TEST_ALNUM: return test_alnum(ch);
        case TEST_NOT_ALNUM: return !test_alnum(ch);
        
        case TEST_WORD: return test_word(ch);
        case TEST_NOT_WORD: return !test_word(ch);

        case TEST_NOT_CRLF: return test_not_crlf(ch);
        case TEST_TRUE: return true;

        default: return false;
        }
    }

    // queue for transition
    void Matcher::queueState(unsigned i, Submatch * s)
    {
        // if already on list with higher priority
        if(visited[i] == stepIndex)
        {
            s->release();
        }
        else
        {
            visited[i] = stepIndex;

            // process empty transitions immediately
            switch(re.states[i].tag)
            {
            case STATE_SPLIT:
                {
                    unsigned n0 = re.states[i].split.next0;
                    unsigned n1 = re.states[i].split.next1;

                    assert(n0 != i && n1 != i && n0 != n1);

#ifdef DEBUG_LORE_EVAL
                    {
                        char buf[123];
                        sprintf(buf, "[%d] queue %d -> %d + %d\n",
                            reclevel, i, n0, n1);
                        dust::debugPrint("%s", buf);
                    }
#endif
                    queueState(n0, s->addRef());
                    queueState(n1, s);
                }
                break;
            case STATE_EMPTY:
                {
#ifdef DEBUG_LORE_EVAL
                    {
                        char buf[123];
                        sprintf(buf, "[%d] queue %d -> %d\n",
                            reclevel, i, re.states[i].empty.next);
                        dust::debugPrint("%s", buf);
                    }
#endif
                    // like split, but only one path
                    queueState(re.states[i].empty.next, s);
                }
                break;
            case STATE_SAVE:
                {
#ifdef DEBUG_LORE_EVAL
                    {
                        char buf[123];
                        sprintf(buf, "[%d] queue %d -> %d (save l%d=%d)\n",
                            reclevel, i,
                            re.states[i].save.next,
                            re.states[i].save.index,
                            position+1);
                        dust::debugPrint("%s", buf);
                    }
#endif
                    // create a copy of the current submatch
                    Submatch * copy = new Submatch(*s);
                    // record new info
                    copy->loc[re.states[i].save.index] = position;
                    // release old
                    s->release();
                    // recurse directly
                    queueState(re.states[i].save.next, copy);
                }
                break;
            default:
                // matches and anything that consumes input
                nlist[i] = s;
                nqueue.push_back(i);
            }
        }
    }

    // this does the main work
    bool Matcher::checkTransition(unsigned i)
    {
#ifdef DEBUG_LORE_EVAL
        {
            dust::debugPrint("[%d] visit %d (state %p)\n",
                reclevel, i, clist[i]);
        }
        assert(cqueue.size() && clist[i]);
        ++reclevel;
#endif

        bool fullMatch = false;

        // otherwise test
        switch(re.states[i].tag)
        {
        case STATE_CHAR:
            if(re.states[i].ch.ch == peek)
                queueState(re.states[i].ch.next, clist[i]);
            else
                clist[i]->release();
            clist[i] = 0;
            break;

        case STATE_CLASS:   // these handle the same
        case STATE_NCLASS:
            {
                bool match = false;

                // take a pointer to the beginning of cdata
                // we are treating the vector as just an array
                const ClassType * cdata =
                    &re.cdata[re.states[i].cdata.cdataIndex];

                // these are fixed positions
                unsigned nChar = cdata[0].num;
                unsigned nRange = cdata[1].num;
                unsigned nFunc = cdata[2].num;
                // skip over them
                cdata += 3;
                for(unsigned j = 0; j < nChar; ++j)
                {
                    if(peek == cdata[0].num)
                    {
                        match = true;
                        goto matchedClass;
                    }
                    cdata += 1;
                }
                for(unsigned j = 0; j < nRange; ++j)
                {
                    // fully inclusive ranges?
                    if(peek >= cdata[0].num
                        && peek <= cdata[1].num)
                    {
                        match = true;
                        goto matchedClass;
                    }
                    cdata += 2;
                }
                for(unsigned j = 0; j < nFunc; ++j)
                {
                    if(call_test(cdata[0].fn, peek))
                    {
                        match = true;
                        goto matchedClass;
                    }
                    cdata += 1;
                }
matchedClass:   // label to allow breaking over all loops
                // check match XOR negated class, but never match EOF
                if(peek != CharEOF
                && match ^ (re.states[i].tag == STATE_NCLASS))
                    queueState(re.states[i].cdata.next, clist[i]);
                else
                    clist[i]->release();
                clist[i] = 0;
            }
            break;
        case STATE_FUNC:
            // do not allow functions to match EOF
            if(peek != CharEOF && call_test(re.states[i].func.func, peek))
                queueState(re.states[i].func.next, clist[i]);
            else
                clist[i]->release();
            clist[i] = 0;
            break;
        case STATE_MATCH:
#ifdef DEBUG_LORE_EVAL
            {
                dust::debugPrint("[%d] match at %d (%d,%d)\n",
                    reclevel, i, clist[i]->loc[0], clist[i]->loc[1]);
            }
#endif
            // if the match is empty, then bail out
            if(clist[i]->loc[0] == clist[i]->loc[1])
            {
                clist[i]->release();
                clist[i] = 0;
                break;
            }
            
            // accept current match as candidate
            if(best)
            {
#ifdef DEBUG_LORE_EVAL
                dust::debugPrint("[%d] previous best (%d,%d)\n",
                    reclevel, best->loc[0], best->loc[1]);
#endif
                best->release();
            }
            best = clist[i]->addRef();
            // clear remaining current states
            for(unsigned j = 0; j < re.states.size(); ++j)
            {
                if(!clist[j]) continue;
                clist[j]->release();
                clist[j] = 0;
            }
            fullMatch = true;
            break;

        default:
            assert(false);
        }
#ifdef DEBUG_LORE_EVAL
        --reclevel;
#endif

        return fullMatch;
    }

    void Matcher::start(PositionType startPos)
    {
        position = startPos;

        // clear nlist, clist and visited
        for(unsigned i = 0; i < re.states.size(); ++i)
        {
            if(nlist[i]) nlist[i]->release();
            nlist[i] = 0;

            if(clist[i]) clist[i]->release();
            clist[i] = 0;

            // clear visited list
            visited[i] = 0;
        }

        if(best) best->release();
        best = 0;

        stepIndex = 1;

        // create the initial sub-match array
        // put to nlist, due to the swap below
        nqueue.clear();
        cqueue.clear();

        queueState(re.first, new Submatch);

        // set started flag, so next() doesn't auto init
        isStarted = true;
    }

    bool Matcher::next(CharType ch)
    {
        // if not started (fresh object, no explicit start() call)
        // then do the default startup from position 0
        if(!isStarted) start();

        // skip the swaps, etc if we don't have a state queue
        if(!nqueue.size()) return true;

#ifdef DEBUG_LORE_EVAL
        {
            char buf[123];
            sprintf(buf, "* TRY: char '%c' at pos %d\n", ch, position);
            dust::debugPrint("%s", buf);
        }
#endif
        peek = ch;

        ++position;
        ++stepIndex;

        // transition next to current
        std::swap(clist, nlist);
        std::swap(cqueue, nqueue);

        // clear next queue
        nqueue.clear();

        for(auto & state : cqueue)
        {
            if(checkTransition(state)) break;
        }

        // return true if nqueue is empty
        return !nqueue.size();
    }

}; // namespace
