
#pragma once

#include <vector>

#include "dust/core/defs.h"

// define for PieceTable::debugSpans() that dumps the contents
// only useful when trying to fix bugs in the piecetable itself
#undef DUST_DEBUG_PTABLE

namespace dust
{
    // PieceTable implements editable text sequences, with built-in
    // support for unlimited undo/redo (with transactions) and
    // selection management.
    //
    // This is designed to act as a backend for text editing.
    //
    class PieceTable
    {
        typedef std::vector<char>  BufferType;

        struct Span
        {
            // Span-chain links
            Span    *next;
            Span    *prev;

            // offset in buffer
            unsigned offset;

            // length of span
            unsigned length;

            // convenience constructor
            Span(unsigned offset, unsigned length)
                : offset(offset), length(length)
            {
                next = prev = 0;
            }

        };

        Span    *head, *tail;

        BufferType  buffer;

        struct {
            // pointer to cached span
            Span    *ptr;
            // cached span start position
            unsigned pos;

        } cache;

        // seeks cache such that pos is either inside
        // the cache span, or right after it
        void seekCache(unsigned pos)
        {
            // seek backwards
            while(cache.pos >= pos)
            {
                if(cache.ptr == head) break;
                cache.ptr = cache.ptr->prev;
                cache.pos -= cache.ptr->length;
            }

            // seek forward
            while(cache.pos + cache.ptr->length < pos)
            {
                cache.pos += cache.ptr->length;
                cache.ptr = cache.ptr->next;
                // guard against overflow
                if(cache.ptr == tail) break;
            }
        }

    public:
        // cursors for undo, etc
        // pos0 is the logical cursor position
        // pos1 is the other marker for selection
        // allow direct manipulation
        struct Cursor
        {
            unsigned pos0, pos1;
        } cursor;
    private:

        // undo types:
        //  insert can:
        //    - split blocks
        //    - adjust end-point (want to append undo)
        //    - add new spans
        //
        //  erase can:
        //    - split blocks
        //    - drop blocks
        //    - adjust end-point
        //
        // the buffer is strictly append only, so no need to touch that
        // so we need the following undo/redo operations:
        //   - split (undo: join?)
        //   - adjust end-point (undo: adjust back)
        //   - add span (undo: delete span)
        //   - delete span (undo: add it back)
        //
        // We also need operations for storing/restoring cursor.
        // We can let undo handle cursor restoration, but we need
        // an explicit cursor adjustment entry for redo().
        //
        struct BaseOp
        {
            Cursor  alt;

            BaseOp(PieceTable & seq)
            {
                alt.pos0 = seq.cursor.pos0;
                alt.pos1 = seq.cursor.pos1;
            }

            virtual ~BaseOp() {}
            virtual void redo(PieceTable & ) = 0;
            virtual void undo(PieceTable & ) = 0;

            void swapCursorXX(PieceTable & seq)
            {
                std::swap(alt.pos0, seq.cursor.pos0);
                std::swap(alt.pos1, seq.cursor.pos1);

                // after swap, place cursor at end of select
                if(seq.cursor.pos0 > seq.cursor.pos1)
                    std::swap(seq.cursor.pos0, seq.cursor.pos1);
            }

            void undoCursor(PieceTable & seq)
            {
                seq.cursor.pos0 = alt.pos0;
                seq.cursor.pos1 = alt.pos1;
            }

            // return true if this op is a mutation of a given span
            // in that case we can modify the span directly as the
            // old values are saved in the existing undo-op
            virtual bool isMutateFor(Span * s) { return false; }
        };

        typedef std::vector<BaseOp*> UndoList;

        // can we insert these at the end of non-empty transactions?
        struct OpCursor : public BaseOp
        {
            OpCursor(PieceTable & seq) : BaseOp(seq) { }

            void undo(PieceTable &) {}
            // we undo cursor only in REDO here,
            // where as other ops restore it in UNDO
            void redo(PieceTable & seq) { undoCursor(seq); }
        };

        struct OpAddSpan : public BaseOp
        {
            Span * span;

            OpAddSpan(PieceTable & seq,
                Span * after, unsigned bufferPos, unsigned length)
                : BaseOp(seq)
            {
                span = new Span(bufferPos, length);
                span->prev = after;
                span->next = after->next;

                redo(seq);
            }

            ~OpAddSpan()
            {
                delete span;
            }

            void redo(PieceTable & seq)
            {
                span->prev->next = span;
                span->next->prev = span;
            }

            void undo(PieceTable & seq)
            {
                span->prev->next = span->next;
                span->next->prev = span->prev;
                undoCursor(seq);
            }
        };

        struct OpDropSpan : public BaseOp
        {
            Span * span;

            OpDropSpan(PieceTable & seq, Span * s) : BaseOp(seq)
            {
                span = s;
                redo(seq);
            }

            void redo(PieceTable & seq)
            {
                span->prev->next = span->next;
                span->next->prev = span->prev;
            }

            void undo(PieceTable & seq)
            {
                span->prev->next = span;
                span->next->prev = span;
                undoCursor(seq);
            }
        };

        struct OpSplit : public BaseOp
        {
            Span * first;
            Span * second;
            unsigned pos;

            OpSplit(PieceTable & seq, Span * span, unsigned at)
                : BaseOp(seq)
            {
                first = span;

                // create first time this is created
                // so future ops can cache pointer
                second = new Span(0, 0);
                pos = at;

                redo(seq);
            }

            // safe to delete in destructor
            ~OpSplit()
            {
                delete second;
            }

            void redo(PieceTable & seq)
            {
                // setup the correct data
                second->offset = first->offset + pos;
                second->length = first->length - pos;

                // link
                second->next = first->next;
                second->next->prev = second;
                second->prev = first;
                first->next = second;

                // fix length of first span
                first->length = pos;
            }

            void undo(PieceTable & seq)
            {
                first->length += second->length;
                first->next = second->next;
                first->next->prev = first;

                undoCursor(seq);
            }
        };

        // adjust length and offset
        struct OpMutate : public BaseOp
        {
            Span * span;
            unsigned altOffset;
            unsigned altLength;

            // this is implemented as a swap back and forth
            void doSwap()
            {
                std::swap(span->offset, altOffset);
                std::swap(span->length, altLength);
            }

            OpMutate(PieceTable & seq,
                Span * s, unsigned newOffset, unsigned newLength)
                : BaseOp(seq)
            {
                span = s;
                altOffset = newOffset;
                altLength = newLength;

                redo(seq);
            }

            void redo(PieceTable & seq) { doSwap(); }
            void undo(PieceTable & seq) { doSwap(); undoCursor(seq); }

            bool isMutateFor(Span * s) { return span == s; }
        };

        UndoList undo, redo;

        // this is used to implement "forgetHistory"
        // which is used by load
        unsigned undoMin;

        // this is 0 if currently saved
        // we increment/decrement this on operations
        unsigned modified;

        void clearRedo()
        {
            // FIXME: does this actually make some sense?!?
            //
            // logic here is that if modified <= undo.size
            // then we can reach not-modified by undo
            // otherwise set modified to a value we can't reach
            if(modified > undo.size()) modified = 0x80000000;

            // clear redo list on new ops
            while(redo.size())
            {
                delete redo.back();
                redo.pop_back();
            }
        }

        // reset the sequence by full undo
        // then clear the redo list and reset buffer
        void clearAll()
        {
            while(undo.size()) doUndo(true);
            clearRedo();

            buffer.clear();
            // free memory as well
            buffer.shrink_to_fit();

            modified = 0;
            undoMin = 0;
        }

        void addUndo(BaseOp * op)
        {
            clearRedo();
            undo.push_back(op);
            if(op) ++modified;
        }

    public:
        enum TransactionType
        {
            TRANSACT_DEFAULT,
            TRANSACT_ERASE,     // allow coalesce with other erases
            TRANSACT_INSERT,    // allow coalesce with other inserts
        };
    private:

        unsigned transactionLevel;
        TransactionType transactionType;

        // transaction management
        void beginAction(TransactionType type)
        {
            if(!transactionLevel++)
            {
                // break transaction if default or different type
                if(type != transactionType || type == TRANSACT_DEFAULT)
                {
                    undo.push_back(0);
                }

                // type rewrite for insert newline
                transactionType = type;
            }
        }

        void endAction()
        {
            if(!--transactionLevel)
            {
                // roll-back empty transactions
                if(undo.size() && !undo.back())
                    undo.pop_back();
            }
        }

        // RAII helper to set cursor after insert/erase
        struct RAIICursorAfterOp
        {
            PieceTable &seq;
            unsigned    pos;
            RAIICursorAfterOp(PieceTable & seq, unsigned pos)
                : seq(seq), pos(pos) {}

            ~RAIICursorAfterOp()
            {
                seq.cursor.pos0 = pos;
                seq.cursor.pos1 = pos;

                seq.addUndo(new OpCursor(seq));
            }
        };
        
    public:

        void saveRedoCursor()
        {
            addUndo(new OpCursor(*this));
        }

        // transactions .. these nest and take care of
        // figuring out if anything was really done
        // so keep one of these around when doing ops
        // and they will get grouped to undo steps
        class RAIIAction
        {
            PieceTable & seq;
        public:
            RAIIAction(PieceTable & seq,
                TransactionType type = TRANSACT_DEFAULT) : seq(seq)
            {
                seq.beginAction(type);
            }
            ~RAIIAction() { seq.endAction();}
        };

        bool isModified()
        {
            return !!modified;
        }

        void setNotModified()
        {
            modified = 0;
            // force transaction boundary
            transactionType = TRANSACT_DEFAULT;
        }

        void forgetHistory()
        {
            undoMin = undo.size();
            // force transaction boundary
            transactionType = TRANSACT_DEFAULT;
        }

        void doUndo(bool force = false)
        {
            unsigned minsize = (force ? 0 : undoMin);
            if(undo.size() > minsize)
            {
                // push a marker to redo list
                redo.push_back(0);
            }

            // don't generate transactions
            ++transactionLevel;

            while(undo.size() > minsize)
            {
                BaseOp * op = undo.back(); undo.pop_back();

                // check for transaction marker
                if(!op) break;

                // undo, then push to redo list
                op->undo(*this); --modified;
                redo.push_back(op);
            }

            // invalidate cache after undo/redo
            cache.ptr = head;
            cache.pos = 0;

            // force transaction boundary
            --transactionLevel;
            transactionType = TRANSACT_DEFAULT;
        }

        void doRedo()
        {
            if(redo.size())
            {
                // push a marker to undo list
                undo.push_back(0);
            }

            ++transactionLevel;

            while(redo.size())
            {
                BaseOp * op = redo.back(); redo.pop_back();

                if(!op) break;

                op->redo(*this); ++modified;
                undo.push_back(op);
            }

            // invalidate cache after undo/redo
            cache.ptr = head;
            cache.pos = 0;

            // force transaction boundary
            --transactionLevel;
            transactionType = TRANSACT_DEFAULT;
        }

        PieceTable()
        {
            // use ~0 as "invalid offset" for these two
            // it simplies other logic below
            head = new Span(~0, 0);
            tail = new Span(~0, 0);

            head->next = tail;
            tail->prev = head;

            cache.ptr = head;
            cache.pos = 0;

            transactionLevel = 0;
            transactionType = TRANSACT_DEFAULT;

            cursor.pos0 = 0;
            cursor.pos1 = 0;

            modified = 0;
            undoMin = 0;
        }

        ~PieceTable() { reset(); }
        void reset() { clearAll(); }

        // insert elements at position or end-of-sequence
        //
        // cursor is set to the end of the inserted text
        void insert(unsigned pos, const char * data, unsigned length)
        {
            // avoid inserting zero-length spans
            // this keeps iterators more simple
            if(!length) return;

            RAIIAction transact(*this, TRANSACT_INSERT);
            RAIICursorAfterOp cursorRedo(*this, pos + length);

            // find the position
            seekCache(pos);

            // make position relative
            Span * span = cache.ptr; pos -= cache.pos;

            // if position is inside span, split it
            if(pos < span->length)
            {
                addUndo(new OpSplit(*this, span, pos));
            }

            // get buffer index for insertion
            unsigned bindex = buffer.size();
            // add data to buffer
            buffer.insert(buffer.end(), data, data+length);

            // check if we can do in-place span extension
            if(span->offset + span->length == bindex)
            {
                // check if top of undo-list can be altered
                BaseOp * op = undo.back();
                if(op && op->isMutateFor(span))
                {
                    clearRedo();    // explicit redo clear
                    span->length += length;
                }
                else
                {
                    BaseOp * mutate = new OpMutate(*this, span,
                        span->offset, span->length + length);
                    addUndo(mutate);
                }
            }
            else
            {
                // this weirdness is for the purpose of having
                // transaction coalescing work.. so need a mutate
                OpAddSpan * add = new OpAddSpan(*this, span, bindex, 0);
                addUndo(add);
                addUndo(new OpMutate(*this, add->span, bindex, length));
            }
        }

        // delete elements from the specified position
        //
        // cursor is set to where the deleted text started
        void erase(unsigned pos, unsigned length)
        {
            // zero-length erases can mess up cursor undo
            // so just skip them completely
            if(!length) return;
        
            RAIIAction transact(*this, TRANSACT_ERASE);
            RAIICursorAfterOp cursorRedo(*this, pos);

            // find the position
            seekCache(pos);

            // make position relative
            Span * span = cache.ptr; pos -= cache.pos;

            if(span == tail) return;

            // are we just beyond current span?
            if(span->length == pos)
            {
                span = span->next;
                pos = 0;
            }

            if(span == tail) return;

            // if we landed mid-span
            if(pos)
            {
                unsigned endPos = pos + length;

                // do we need to split?
                if(endPos < span->length)
                {
                    BaseOp * split = new OpSplit(*this, span, endPos);
                    addUndo(split);

                    BaseOp * mutate = new OpMutate(*this, span, span->offset, pos);
                    addUndo(mutate);

                    return;
                }
                else
                {
                    length -= span->length - pos;

                    // check if top of undo-list can be altered
                    BaseOp * op = undo.back();
                    if(op && op->isMutateFor(span))
                    {
                        clearRedo();    // explicit redo clear (since no AddUndo())
                        span->length = pos;
                    }
                    else
                    {
                        BaseOp * mutate = new OpMutate(*this, span, span->offset, pos);
                        addUndo(mutate);
                    }
                    span = span->next;
                }
            }

            // drop any completely covered spans
            while(length && length >= span->length)
            {
                // check end of sequence
                if(span == tail) return;

                length -= span->length;
                BaseOp * drop = new OpDropSpan(*this, span);
                addUndo(drop);

                // if we're dropping a cached span, step back a bit
                // OpDropSpan doesn't destroy next/prev pointers
                if(cache.ptr == span)
                {
                    cache.ptr = span->prev;
                    cache.pos -= cache.ptr->length;
                }
                // next span
                span = span->next;
            }

            // adjust the end of region
            if(length)
            {
                // check if top of undo-list can be altered
                BaseOp * op = undo.back();
                if(op && op->isMutateFor(span))
                {
                    clearRedo(); // explicit redo-clear

                    span->offset += length;
                    span->length -= length;
                }
                else
                {
                    BaseOp * mutate = new OpMutate(*this, span,
                        span->offset + length, span->length - length);
                    addUndo(mutate);
                }
            }
        }

        // erase current selection if any
        // return true if there was one
        bool eraseSelection()
        {
            if(cursor.pos0 == cursor.pos1) return false;

            unsigned begin = cursor.pos0;
            unsigned end = cursor.pos1;
            if(begin > end) std::swap(begin, end);

            unsigned len = end - begin;
            if(len)
            {
                erase(begin, len);
            }

            return true;
        }

        // return pointer to element at position
        const char * getElementAt(unsigned pos)
        {
            // find the desired position
            seekCache(pos);

            // first find the desired position
            Span * span = cache.ptr; pos -= cache.pos;

            if(span == tail) return 0;

            if(span->length == pos)
            {
                span = span->next;
                pos = 0;
            }

            if(span == tail) return 0;

            return & buffer[span->offset + pos];
        }

        unsigned getSize() const
        {
            unsigned size = 0;
            Span * span = head;
            while(span != tail)
            {
                size += span->length;
                span = span->next;
            }
            return size;
        }

        struct Iterator
        {
            Iterator(const PieceTable * table, Span * span)
            : table(table), span(span)
            {
                index = 0;
            }

            // only support pre-increment?
            void operator++()
            {
                if(++index == span->length)
                {
                    span = span->next;
                    index = 0;
                }
            }

            char operator*() const { return getChar(); }

            bool operator==(const Iterator & other) const
            {
                return span == other.span && index == other.index;
            }
            bool operator!=(const Iterator & other) const
            {
                return span != other.span || index != other.index;
            }

        private:

            const PieceTable *table;

            Span        *span;
            unsigned    index;

            char getChar() const
            {
                return table->buffer[span->offset + index];
            }
        };

        Iterator begin() const { return Iterator(this, head->next); }
        Iterator end() const { return Iterator(this, tail); }


#ifdef DUST_DEBUG_PTABLE
        void debugSpans() const
        {
            debugPrint("Current spans:\n");

            unsigned index = 0;
            unsigned position = 0;

            Span *span = head;
            while(span)
            {
                debugPrint(" #%d: %8x (p:%8x,n:%8x), pos: %d - len:%d - at:%d|%s\n",
                    index, span, span->prev, span->next, position, span->length, span->offset,
                    span == head ? "head" : (span == tail ? "tail" : ""));

                ++index;
                position += span->length;
                span = span->next;
            }

            {
                debugPrint(" cache: %8x at %d\n", cache.ptr, cache.pos);
            }

            debugPrint(" cursor: %d-%d)", cursor.pos0, cursor.pos1);
        }
#endif
    };


}; // namespace
