
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

#pragma once

#include <vector>   // used for most internal memory management
#include <string>
#include <cassert>

/*

 Dev notes:
------------

 this isn't really production quality code yet .. it appears to
 work but has not been extensively tested and API is subject to
 change (probably just extended a bit, but you have been warned)

 eventually this should (optionally) decode utf-8 to code-points,
 but for now it only handles raw binary data and ASCII text (poorly)
 with all "high-ascii" treated as "word characters"

 if you are wondering: why another regex library, then read below;

 TL;DR: I wanted "character at a time" search with strict complexity
 bounds and a simple and flexible API that would work for anything

 Lore (extended) introduction
------------------------------

 Lore stands for "linear-time online regular expressions" and the
 name describes the basic concept pretty well: search on-the-fly
 and make sure that the engine can "never" get stuck or fail.

 On top of that is is simple C++98 and "free-standing" without
 external dependencies (it uses STL internally, but that's it).

 For an expression of length k it uses O(k) time per input character
 and keeps O(k) state. It runs incrementally one character at a time.
 Note that the O(k) bound is measured in regex-pattern string length.
 If this doesn't sound useful, you probably want some other library.

 In terms of input string length, is uses O(n) time and O(1) space.
 There are legends in the CS lore claiming that this is possible.

 Lore works by doing a "breath-first" search into the regex graph and
 never needs to backtrack: it searches all possible paths in parallel.
 All paths advance in lock-step and incremental execution is "free"
 and in fact the primary reason that Lore exists at all.

 This means it can only implement "true" regular expressions that can
 be converted into (non-deterministic) finite state machines. If you
 need back-references or some other "non-regular" features, then this
 library is not (and will probably never become) useful for you.

 Note that information about sub-match captures is still tracked, so
 the usual pattern subsdustutions are possible (the actual processing
 of such subsdustutions is outside the scope of this library; adding
 a convenience wrapper/example into lore::Matcher is possible though).

 The algorithm used is not novel at all. See the excellent articles
 by Russ Cox (http://swtch.com/~rsc/regexp/) for some documents about
 this type of strategy. What Lore tries to provide is flexible API
 with building blocks for different uses cases, without treating the
 search process as an atomic "blocking" operation.

 In other words: Lore is designed to make regular expressions easy
 to use in situations where existing library APIs are inconvenient
 and with predictable performance that doesn't lead to surprises.

 Whenever a regular expression is successfully compiled (valid syntax)
 no further errors are possible and matching will always terminate for
 any finite input; in theory throwing std::bad_alloc is possible, but
 in practice the memory requirements have a small O(k) upper bound.

 FIXME: there is a theoretical failure with complex expressions where
 Matcher::queueState() might overflow a small stack when the compiler
 creates a long chain of split/empty nodes without any match nodes;
 the maximum recursion has O(k) theoretical bound so for reasonable
 pattern lengths that should not be a practical issue

 If the syntax is invalid, it returns an error message, but also the
 parser's internal position counter (location of error) such that an
 interactive applications can report this accurately; for interactive
 use you can easily cook this into a "your error is here" display.

 It is also possible to match infinite (or out-of-core) inputs, since
 the engine runs incrementally and only tracks positions for the match
 boundaries (which are bounded by regex size and independent of input).
 In this case one should make sure that PositionType is large enough
 to avoid overflow (eg. make it 64-bit or something), but it is not
 necessary for the actual tested (or matched) string to fit memory.

 Any lore::Regex is constant after construction and safe to use from
 multiple threads at the same time as long as each one keeps it's own
 lore::Matcher state object (one per on-going search in case you want
 to keep track of multiple searches in parallel).

 Both objects follow the standard RAII principle and can be allocated
 in stack or heap. The lore::Regex must stay alive until lore::Matcher
 objects referring it are destroyed (matchers keep a reference), but
 otherwise they don't really have any specific requirements.

 They allocate some heap memory with default new/new[]/delete/delete[],
 so for performance avoid creating fresh copies for each loop iteration
 (restart an existing matcher at a new position instead; this is always
 safe at any point, whatever the current state of the matcher).

 Global lore::Regex objects (eg. constants patterns that are compiled
 during application start-up automatically) are also supposed to work
 as long as the pattern is constant or otherwise guaranteed to have
 been constructed at earlier time (string literals and such are fine).

 If you want to hack the parser: for each sub-group (and full match)
 it calls parse() recursively, with rest of the parse done using the
 compiler "stack" of fragments with a shift-reduce strategy; the two
 variables "seq" and "alt" track how many sequential and alternative
 matches we have in stack, with sequences reduced to alternatives and
 alternatives merged into sub-matches to return to upper level.


 Quick API tutorial
--------------------

 // some scope where the objects can exist
 {
    lore::Regex re("myPattern"); // compile Regex object

    // is there a syntax problem
    if(re.error())
    {
        // user friend errors with position where the
        // error was detected (for visual feedback, etc)
        printf("regex has error '%s' at position %d\n",
            re.error(), re.errorOffset());
    }

    lore::Matcher m(re);         // create matcher state

    // start a new match from a logical application position
    // calling .start() also clears any existing match data
    // and can be called at any time to start a new match
    // this isn't strictly necessary the first time around
    // if you are happy with positions starting from zero
    m.start(pos);

    // main loop; running forever is supported use-case
    // but see notes below about changing PositionType
    // if you expect to search with over 4GB in one pass
    while(true)
    {
        if(endOfApplicationData())
        {
           m.end(); // notify matcher about end of data
           break;   // break out of match loop
        }

        // feed matcher next input character, if it returns
        // true we can (but don't really need to) break
        if(m.next( getNextApplicationCharacter() )) break;

        if(valid())
        {
            // we have a match, but as long as next()
            // returns false there is a possibility that
            // we might eventually find a better one
        }
    }
    if(valid())
    {
        // here the current match is the best possible
        // if there was no match, then valid() == false
        printf("match from %d to %d\n",
            m.getGroupStart(0), m.getGroupEnd(0));
    }
 }


 Accepted regex syntax (roughly):
----------------------------------

  | () (?:a)    alternates, grouping (with and without capture)
  a? a* a+      match 0-or-1, match 0-or-more, match 1-or-more
  a?? a*? a+?   non-greedy versions (prefer shorter match)

  ^ $   anchors: ^ at beginning match beginning, $ at end match end
  .     match any character, except "newline" (either '\n' or '\r')

     note: bounded repedustions are explicitly NOT supported, since
     they result in O(2^k) worse-case bound on automata size losing
     the O(k) bound that gives application control over memory use;
     an extension is possible, but that should never be the default
     since it essentially makes all the complexity bounds irrelevant
     (if this doesn't make sense, just use another library instead)

  character classes:
     [abc]      match any of "a" "b" "c"
     [^abc]     negated class
     [a-z]      match "a" to "z" (by numerical values)

     all escapes are valid in character classes, except in ranges
     ranges only allow normal escapes that expand into characters

     ranges are direction agnostic: [a-z] is the same as [z-a]

     combinations are valid, eg [^a-z\d] would match any character
     that is not within the a-z range, and not digit

  escapes:
     \-prefixed non-alphanumerals match literally
     regular string-escapes ( \t, \e, \n, \r, \0 ) work as usual

     Note: for literal pattern strings in source, one can use an
     alternate escape character and avoid some \\\\ nonsense

  special escapes:
     \d, \D     digit, not-digit
     \s, \S     whitespace, not-whitespace
     \w, \W     word-char, not word-char

     word-characters are ASCII alphanum, underscore and "high-ASCII"

     adding more is easy, see parse_escape_raw(..) in lore_compile.cpp

  extra notes:

     the beginning anchor ^ will always match at the beginning of the
     current search (by design, since this can be useful); for normal
     behavior you can check Regex::onlyAtBeginning() and if it returns
     true, only start a search where matching ^ is desired

     the ending anchor $ will only match at the end() of a search; for
     single line mode, split the search and call Matcher::end() after
     each line, then start a new search on the next line

     priorities with sub-groups follow "PCRE rules" (assuming no bugs)
     and we return whatever a back-tracking algorithm would return
     so greedy/non-greedy choices in sub-groups have higher priority
     than any outer greedy/non-greedy choices even if the match is not
     the longest possible, eg: search (a.?)* in "aaba" returns "aa"

     also by "PCRE rules" sub-groups return the last instance of the
     sub-match, eg: \1 from search (.)* in "ab" is "b"

     the general rule is "when in doubt, follow PCRE rules" and
     any future improvements should try to follow this rule unless
     there is a ReallyGoodReason(tm) for an alternative behavior;
     ideally the alternative behaviour should be an error message

     an example of such a reason would be losing the time or space
     complexity bounds (if you don't care, just use PCRE instead);
     another valid reason would be "too much code complexity without
     enough practical value" which applies for many missing features

     in general though, trying to follow PCRE rules has benefits:

      1. it is less confusing to people who already understand those
         rules (at least assuming someone actually understands them)

      2. we can point fingers: see, PCRE does the same thing

      3. we can claim that future extensions breaking the existing
         syntax are actually just bug-fixes for PCRE compatibility

     in other words, try not to rely on syntax that has a different
     meaning in Lore and PCRE; it might (or might not) get fixed or
     in some cases explicitly rejected in future versions


  Performance Tips
 ------------------

   Every regex engine seems to have a "performance tips" section
   in it's documentation.

   Lore runs in O(n*k) time for n=len(input), k=len(regex) so the
   main thing you can do is make your regex and/or input shorter.

   The structure of the regex is largely irrelevant, the shortest
   way to express something should normally be the fastest too or
   at least close enough that it doesn't really matter.

   Don't use Matcher::search() if you want performance, it is just
   provided as an example of how to perform a search.

   Capture groups must allocate/copy state-blocks at boundaries,
   so avoiding captures in inner "loops" will speed things up.
   Using (?:foo)* instead of (foo)* is slightly faster and if you
   want to capture then ((?:foo)*) is often more useful anyway.

   Since PCRE returns the last iteration of the sub-match, there
   is very little we can do here without giving up compatibility.
   Note that (in theory, see below) this doesn't really violate
   the complexity bound (just larger hidden constants).

   FIXME: for strict complexity bounds, should really pre-allocate
   for the worst-case or at least keep a local cache, since in the
   strict sense new/delete are nowhere near O(1) operations and
   even in practice it makes no sense to reallocate all the time

*/

namespace lore
{

    // position type, feel free to make this something larger
    typedef unsigned    PositionType;

    // internal character type, separate from interface type
    // must be unsigned and should fit values larger than the
    // maximum character in the character set.. so really 4 bytes
    typedef unsigned    CharType;

    // this should be an invalid character, don't use '\0'
    // CharEOF never matches anything except the special $ anchor
    static const unsigned CharEOF = ~0;

    //////////////////////////////////////////////////////////////////
    // FIXME: everything below except lore::Regex and lore::Matcher //
    // should really be moved into an internal namespace (eg. impl) //
    //////////////////////////////////////////////////////////////////

    // Straight-forward Tomphson NFA implementation.
    //
    // States can be either:
    //  - character: test
    //  - group: fancy membership test
    //  - function: character class test
    //  - empty/split: recurse one or two targets
    //  - save: works like empty but records sub-group info
    //  - match: store the current best match
    //
    enum StateType
    {
        STATE_CHAR,     // match single character

        STATE_CLASS,    // match with [abc] class
        STATE_NCLASS,   // negated [^abc] class

        STATE_FUNC,     // match using a function test

        STATE_SPLIT,    // split execution to two paths
        STATE_EMPTY,    // empty match (to merge paths)

        STATE_SAVE,     // state save (sub-match boundary)
        STATE_MATCH,    // accept a match

        STATE_COUNT
    };

    // NOTE: This used to be a function pointer, but in 64-bit that's
    // less than ideal as it bloats the CData to twice the size and
    // we only have finite number of classes to test anyway.
    enum TestFunc
    {
        TEST_WHITE, TEST_NOT_WHITE,
        TEST_DIGIT, TEST_NOT_DIGIT,
        TEST_ALNUM, TEST_NOT_ALNUM,
        TEST_WORD,  TEST_NOT_WORD,

        TEST_NOT_CRLF, TEST_TRUE
    };

    // ClassType is used to store data for [] classes
    //
    // Any class can be an arbitrary combination of:
    //  - charaters
    //  - ranges of characters
    //  - built-in classes
    //  - negation (handled as another state type)
    //
    // For each class, we pack data into Regex::cdata vector
    // simply dumping the data there, and saving the beginning
    // index into the state-node.
    //
    // First three values dumped at the number of character
    // number of ranges and number of functions.
    //
    // After the counters, we simply store characters one by one
    // then after all characters, ranges with begin, end for each,
    // then after all the ranges, the function pointers.
    //
    union ClassType
    {
        unsigned    num;
        TestFunc    fn;

        // simplify .push_back()
        ClassType(unsigned num) : num(num) {}
        // simplify .push_back()
        ClassType(TestFunc fn) : fn(fn) {}
    };

    // StateNode represents an NFA state; stored in vector as is
    // so keep these small and store larger data elsewhere
    struct StateNode
    {
        StateType tag;

        struct StateChar
        {
            CharType ch;
            unsigned next;
        };

        struct StateClass
        {
            unsigned cdataIndex;
            unsigned next;
        };

        struct StateFunc
        {
            TestFunc func;
            unsigned next;
        };

        struct StateSplit
        {
            unsigned next0;
            unsigned next1;
        };

        struct StateEmpty
        {
            unsigned next;
        };

        struct StateSave
        {
            unsigned index;
            unsigned next;
        };

        union
        {
            StateChar   ch;
            StateClass  cdata;
            StateFunc   func;
            StateSplit  split;
            StateEmpty  empty;
            StateSave   save;

            // Note: match doesn't need data
        };
    };

    // forward defined
    class Matcher;

    // Regex is a compiled state machine
    //
    class Regex
    {
        friend class Matcher;

        // vector of FSM states
        std::vector<StateNode> states;

        // vector of combined char-class data
        // this simply holds the data for simple
        // automatic deallocation on destructor
        std::vector<ClassType> cdata;

        unsigned first;     // starting state
        const char * errorString;
        unsigned errorPos;

        bool hasBeginAnchor;

        void compile(char escapeChar, const char * pattern, unsigned len);

    public:

        // basic c-strings
        Regex(const char * pattern)
        {
            int sz = 0;
            while(pattern[sz]) ++sz;
            compile('\\', pattern, sz);
        }

        // explicit length - allow embedded nulls
        Regex(const char * pattern, unsigned len)
        {
            compile('\\', pattern, len);
        }

        // std::string - allow embedded nulls
        Regex(const std::string & pattern)
        {
            compile('\\', pattern.c_str(), pattern.size());
        }

        // basic c-strings with custom escape
        // this is intended for source code
        Regex(char escapeChar, const char * pattern)
        {
            int sz = 0;
            while(pattern[sz]) ++sz;
            compile(escapeChar, pattern, sz);
        }

        // returns null if compile succeeded
        const char * error() const
        {
            return errorString;
        }

        // if there was an error, returns offset in pattern
        // this is indended for interactive reporting :)
        unsigned errorOffset() const { return errorPos; }

        // return true if the pattern starts with ^ anchor
        bool onlyAtBeginning() const { return hasBeginAnchor; }
    };

    // Matcher is the machine current machine state
    class Matcher
    {
        const Regex & re;

        // Track sub-group matches as a fixed array for now
        // Really want to resize this depending on Regex
        struct Submatch
        {
            unsigned refCount;
            PositionType loc[20]; // \0..\9

            Submatch()
            {
                refCount = 1;

                // fill with invalid ranges
                for(int i = 0; i < 20; ++i)
                {
                    if(i&1) loc[i] = 0;
                    else loc[i] = ~0;
                }
            }

            // copy constructor
            Submatch(const Submatch & other)
            {
                refCount = 1;

                for(int i = 0; i < 20; ++i)
                {
                    loc[i] = other.loc[i];
                }
            }

            Submatch * addRef() { ++refCount; return this; }
            void release() { if(!--refCount) delete this; }
        };

        // current best match
        Submatch * best;

        // current and next state
        // arrays of pointers to submatch candidates
        //  - null value means state is inactive
        Submatch **clist;
        Submatch **nlist;

        // array of positions last visited position per state
        unsigned *visited;

        // use private counter for visited, so we don't place
        // any internal requirements on position values
        PositionType stepIndex;

        // current and next queue with current sizes
        unsigned *cqueue, csize;
        unsigned *nqueue, nsize;

        // stored peak-ahead character
        CharType peek;

        // current position in stream, application specific
        // and only used to report sub-match boundaries
        PositionType position;

        // this flag is bool if we have initialized
        // want to delay default initialization from
        // constructor to skip doing it twice if the
        // user calls start() explicitly on first use
        bool isStarted;

        void checkTransition(unsigned i);
        void queueState(unsigned i, Submatch * s);

        // do not allow copies
        Matcher(const Matcher & m) = delete;
    public:
        Matcher(const Regex & re) : re(re)
        {
            // FIXME: this crap isn't exception safe
            // should really convert to RAII internally
            // want to keep it C++98 so need custom crap
            //
            // Fixing the on-the-fly Submatch allocs
            // is kinda higher priority though, this
            // note is here just as a reminder.
            clist = new Submatch*[re.states.size()];
            nlist = new Submatch*[re.states.size()];
            visited = new unsigned[re.states.size()];
            cqueue = new unsigned[re.states.size()];
            nqueue = new unsigned[re.states.size()];

            for(unsigned i = 0; i < re.states.size(); ++i)
            {
                clist[i] = 0;
                nlist[i] = 0;
            }

            position = 0;
            best = 0;

            csize = nsize = 0;
            isStarted = false;
        }

        ~Matcher()
        {
            // clear nlist, clist is always clear
            for(unsigned i = 0; i < re.states.size(); ++i)
            {
                if(nlist[i]) nlist[i]->release();
            }
            if(best) best->release();

            delete []clist;
            delete []nlist;
            delete []cqueue;
            delete []nqueue;
            delete []visited;
        }

        // start a new match from the given logical starting position
        // this "position" counter is only used for reporting submatches
        // so it has no effect on any internal operation
        void start(PositionType startPos = 0);

        // send a character to the matcher
        // if start is true, this starts a new match
        //
        // returns false if additional input is required
        // to decide the best match, true if the best match
        // was found, or no match is possible
        //
        // once the function returns true, calling it again
        // without setting start = true doesn't do anything
        //
        bool next(CharType ch);

        // overload manually so we get unsigned conversion
        bool next(char ch) {
            return next(CharType((unsigned char) ch));
        }

        // tell the matcher that we finished
        // returns true if we found a match
        bool end()
        {
            // flush the input with EOF until
            while(!next(CharEOF));
            return valid();
        }

        // this is a simplified interface (and example)
        // returns true if match is found, false otherwise
        bool search(const std::string & str)
        {
            start();    // restart
            for(unsigned i = 0; i < str.size(); ++i)
            {
                // break as soon as we match :)
                if(next(str[i]))
                {
                    // early exit
                    return valid();
                }
            }

            end();
            return valid();
        }

        // like the std::string version, but null-terminated
        bool search(const char * str)
        {
            start();    // restart
            while(*str)
            {
                if(next(*str))
                {
                    // early exit
                    return valid();
                }
                ++str;
            }

            end();
            return valid();
        }

        // return true if there is a valid match available
        bool valid() const { return 0 != best; }

        // return beginning position for a given submatch
        // if (start > end) then a group didn't match
        PositionType getGroupStart(unsigned g) const
        {
            assert(g < 10);
            return best->loc[(g<<1)];
        }

        // return ending position for a given submatch
        PositionType getGroupEnd(unsigned g) const
        {
            assert(g < 10);
            return best->loc[(g<<1)+1];
        }
    };

};
