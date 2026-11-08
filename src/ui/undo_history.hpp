#pragma once
// Pure (no ImGui) undo/redo history for the graphical configurator.
// Holds deep copies (nlohmann::json values) of the whole document, so
// an undo step restores the document exactly as it was, including
// nested sub-registers, hex strings and raw peripheral JSON.
//
// Model: `past_` is a stack whose top is always the current document
// state. A new edit is recorded by pushing the document AFTER the
// mutation (see below); undo pops the top onto the redo stack and
// returns the new top, redo moves it back. The saved state is a
// separate snapshot used only to restore the dirty flag.
//
// Why push-after, not push-before: undo() and redo() take no document
// argument, so the history itself must own the current state --
// otherwise undo could not know what to put on the redo stack. A
// push of the pre-edit document before a mutation is therefore NOT
// a substitute: it is a no-op (dedup, see push()) when the history
// is already up to date, and on its own it leaves undo with nothing
// to return. The invariant the UI must keep is: after every
// mutation, and after every load, the top of `past_` equals
// `state.doc`. The pre-edit state that undo restores to is the
// snapshot pushed after the previous edit (or at load time), which
// is exactly the state before this edit. Callers push the
// post-mutation document; see the UI edit specification for the
// call sites.
//
// Dedup: pushing a document equal to the current top is a no-op, so
// callers may push unconditionally after a widget (a Combo or
// InputInt that did not change leaves the document equal to the
// top). Text fields are the exception: the document changes on every
// keystroke, so they push only once, when the edit commits (see
// IsItemDeactivatedAfterEdit in the UI spec), never per keystroke.

#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

class UndoHistory {
  public:
    // Maximum number of snapshots kept (the current state counts
    // toward it, so at most 49 undo steps are available when full).
    static constexpr size_t kMaxSnapshots = 50;

    // Record `doc` as the new current state. No-op when `doc`
    // equals the current top (nothing changed). Drops the oldest
    // snapshot when the cap would be exceeded, and clears the redo
    // stack: a new edit starts a new branch.
    void push(const nlohmann::json &doc) {
        if (!past_.empty() && past_.back() == doc)
            return;
        past_.push_back(doc);
        while (past_.size() > kMaxSnapshots)
            past_.erase(past_.begin());
        future_.clear();
    }

    bool canUndo() const { return past_.size() > 1; }
    bool canRedo() const { return !future_.empty(); }

    // Undo one step: move the current state onto the redo stack and
    // return the previous state. Returns nullopt when there is
    // nothing to undo (zero or one snapshots).
    std::optional<nlohmann::json> undo() {
        if (!canUndo())
            return std::nullopt;
        future_.push_back(past_.back());
        past_.pop_back();
        return past_.back();
    }

    // Redo one step: move the most recently undone state back onto
    // the past stack and return it. Returns nullopt when there is
    // nothing to redo.
    std::optional<nlohmann::json> redo() {
        if (!canRedo())
            return std::nullopt;
        past_.push_back(future_.back());
        future_.pop_back();
        return past_.back();
    }

    // Drop every snapshot and forget the saved state.
    void clear() {
        past_.clear();
        future_.clear();
        saved_.reset();
    }

    // Record the current state (top of the past stack) as the saved
    // state. No-op when the history is empty: call push(doc) first
    // (LoadFromFile / LoadTemplate do exactly that).
    void markSaved() {
        if (!past_.empty())
            saved_ = past_.back();
    }

    // True when `current` equals the state recorded by the last
    // markSaved(). False when nothing has been marked saved yet.
    // The UI sets `dirty = !history.isSaved(doc)` after undo/redo.
    bool isSaved(const nlohmann::json &current) const {
        return saved_.has_value() && *saved_ == current;
    }

    // Number of snapshots on the past stack (1 once the initial
    // document has been pushed, 0 after clear()). For tests and
    // for the UI to sanity-check the "top equals doc" invariant.
    size_t size() const { return past_.size(); }

    // The current state (top of the past stack), nullopt when empty.
    std::optional<nlohmann::json> current() const {
        if (past_.empty())
            return std::nullopt;
        return past_.back();
    }

  private:
    std::vector<nlohmann::json> past_;   // top == current document
    std::vector<nlohmann::json> future_; // states undone from, redo order
    std::optional<nlohmann::json> saved_;
};
