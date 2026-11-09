// Tests for the configurator undo/redo history
// (src/ui/undo_history.hpp): push/undo/redo order and values, the
// 50-snapshot cap, dedup of consecutive identical pushes, a new push
// clearing redo, and markSaved/isSaved behaviour.
#include "ui/undo_history.hpp"
#include <cassert>
#include <iostream>

using nlohmann::json;

static json Doc(int v) { return json{{"v", v}}; }

int main() {
    // Push/undo/redo order and values.
    {
        UndoHistory h;
        assert(!h.canUndo());
        assert(!h.canRedo());
        assert(!h.undo().has_value());
        assert(!h.redo().has_value());
        assert(h.size() == 0);
        assert(!h.current().has_value());

        h.push(Doc(1));
        assert(h.size() == 1);
        assert(h.current() == Doc(1));
        assert(!h.canUndo()); // one snapshot: nothing to undo to
        assert(!h.undo().has_value());

        h.push(Doc(2));
        h.push(Doc(3));
        assert(h.canUndo());
        assert(!h.canRedo());

        std::optional<json> back = h.undo();
        assert(back.has_value() && *back == Doc(2));
        assert(h.canUndo());
        assert(h.canRedo());
        assert(h.current() == Doc(2));

        back = h.undo();
        assert(back.has_value() && *back == Doc(1));
        assert(!h.canUndo());
        assert(h.canRedo());
        assert(!h.undo().has_value()); // past the oldest: no-op

        back = h.redo();
        assert(back.has_value() && *back == Doc(2));
        back = h.redo();
        assert(back.has_value() && *back == Doc(3));
        assert(!h.canRedo());
        assert(!h.redo().has_value()); // past the newest: no-op
        assert(h.current() == Doc(3));
    }

    // Consecutive identical pushes are deduped; a non-consecutive
    // repeat is a real new state and is kept.
    {
        UndoHistory h;
        h.push(Doc(1));
        h.push(Doc(1));
        h.push(Doc(1));
        assert(h.size() == 1);
        assert(!h.canUndo());

        h.push(Doc(2));
        h.push(Doc(1)); // same value as two pushes ago: kept
        assert(h.size() == 3);
        assert(h.undo() == Doc(2));
        assert(h.undo() == Doc(1));
    }

    // A new push after undo clears the redo stack.
    {
        UndoHistory h;
        h.push(Doc(1));
        h.push(Doc(2));
        h.push(Doc(3));
        assert(h.undo() == Doc(2));
        assert(h.canRedo());
        h.push(Doc(4)); // branches: the undone Doc(3) is gone
        assert(!h.canRedo());
        assert(!h.redo().has_value());
        assert(h.undo() == Doc(2));
        assert(h.undo() == Doc(1));
    }

    // Cap at 50 snapshots: the oldest are dropped, the newest kept.
    {
        UndoHistory h;
        for (int i = 0; i < 55; ++i)
            h.push(Doc(i));
        assert(h.size() == 50);
        assert(h.current() == Doc(54));
        // 49 undos walk back to Doc(5); the 50th has nowhere to go.
        for (int expect = 53; expect >= 5; --expect) {
            std::optional<json> back = h.undo();
            assert(back.has_value() && *back == Doc(expect));
        }
        assert(h.current() == Doc(5));
        assert(!h.canUndo());
        assert(!h.undo().has_value());
        // Redo walks all the way back to the newest.
        for (int expect = 6; expect <= 54; ++expect) {
            std::optional<json> fwd = h.redo();
            assert(fwd.has_value() && *fwd == Doc(expect));
        }
        assert(!h.canRedo());
        // One more push at the cap still keeps exactly 50.
        h.push(Doc(55));
        assert(h.size() == 50);
        assert(h.current() == Doc(55));
    }

    // markSaved / isSaved: saved, then changed = not saved, undo
    // back = saved again. Saving a later state moves the mark.
    {
        UndoHistory h;
        assert(!h.isSaved(Doc(1))); // nothing marked saved yet
        h.markSaved();               // empty history: no-op
        assert(!h.isSaved(Doc(1)));

        h.push(Doc(1));
        h.markSaved();
        assert(h.isSaved(Doc(1)));
        assert(!h.isSaved(Doc(2)));

        h.push(Doc(2));
        assert(!h.isSaved(Doc(2)));
        assert(h.undo() == Doc(1));
        assert(h.isSaved(Doc(1))); // undo back to the saved state

        h.push(Doc(3)); // new edit clears redo, still not saved
        assert(!h.isSaved(Doc(3)));
        h.markSaved();
        assert(h.isSaved(Doc(3)));
        assert(!h.isSaved(Doc(1)));
    }

    // isSaved compares by value, not by which snapshot object: a
    // freshly built document equal to the saved one counts as saved.
    {
        UndoHistory h;
        h.push(json::parse(R"({"a": [1, 2], "b": {"c": "x"}})"));
        h.markSaved();
        assert(h.isSaved(json::parse(R"({"b": {"c": "x"}, "a": [1, 2]})")));
        assert(!h.isSaved(json::parse(R"({"a": [1, 2], "b": {"c": "y"}})")));
    }

    // clear() drops everything, including the saved mark.
    {
        UndoHistory h;
        h.push(Doc(1));
        h.push(Doc(2));
        h.markSaved();
        h.clear();
        assert(h.size() == 0);
        assert(!h.canUndo());
        assert(!h.canRedo());
        assert(!h.undo().has_value());
        assert(!h.redo().has_value());
        assert(!h.isSaved(Doc(1)));
        // Usable again after clear.
        h.push(Doc(9));
        assert(h.size() == 1);
        assert(h.current() == Doc(9));
    }

    std::cout << "All undo history tests passed!" << std::endl;
    return 0;
}
